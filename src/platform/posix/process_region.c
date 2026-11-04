#define _GNU_SOURCE

#include "minios/process_region.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MINIOS_PROCESS_REGION_MAGIC UINT64_C(0x4d494e4950524547)
#define MINIOS_PROCESS_MODEL_KIND 1u
#define MINIOS_PROCESS_PROGRESS_KIND 2u
#define MINIOS_PROCESS_ABI_VERSION 1u

typedef struct {
    uint64_t magic;
    uint32_t abi_version;
    uint32_t kind;
    uint32_t header_size;
    uint32_t reserved;
    uint64_t region_size;
    uint64_t run_id;
} MiniOSProcessRegionHeader;

typedef struct {
    MiniOSProcessRegionHeader common;
    uint32_t model_version;
    uint32_t width;
    uint32_t height;
    uint32_t reserved;
    uint64_t element_count;
    uint64_t weights_offset;
    uint64_t weights_bytes;
} MiniOSProcessModelHeader;

typedef struct {
    uint32_t actor_id;
    uint32_t generation;
    uint32_t state;
    uint32_t completed;
    uint32_t total;
    uint32_t reserved;
    uint64_t sequence;
    uint64_t result;
} MiniOSProcessSharedProgress;

typedef struct {
    MiniOSProcessRegionHeader common;
    uint64_t model_run_id;
    uint32_t actor_count;
    uint32_t update_capacity;
    pthread_mutex_t mutex;
    sem_t updates_ready;
    sem_t free_slots;
    uint64_t next_global_sequence;
    uint64_t next_read_sequence;
    MiniOSProcessSharedProgress actors[MINIOS_PROCESS_REGION_MAX_ACTORS];
    MiniOSProcessUpdate updates[MINIOS_PROCESS_UPDATE_CAPACITY];
} MiniOSProcessProgressHeader;

static int valid_name(const char *name)
{
    size_t length;
    size_t index;

    if (name == NULL || name[0] != '/') {
        return 0;
    }

    length = strlen(name);
    if (length < 2u || length >= MINIOS_PROCESS_REGION_NAME_MAX) {
        return 0;
    }
    for (index = 1u; index < length; ++index) {
        if (name[index] == '/') {
            return 0;
        }
    }
    return 1;
}

static int copy_name(char destination[MINIOS_PROCESS_REGION_NAME_MAX],
                     const char *source)
{
    size_t length;

    if (!valid_name(source)) {
        return 0;
    }
    length = strlen(source);
    memcpy(destination, source, length + 1u);
    return 1;
}

static int checked_model_size(uint32_t width,
                              uint32_t height,
                              size_t *element_count,
                              size_t *weights_bytes,
                              size_t *weights_offset,
                              size_t *region_size)
{
    size_t count;
    size_t bytes;
    size_t offset;

    if (width == 0u || height == 0u ||
        (size_t)width > SIZE_MAX / (size_t)height) {
        return 0;
    }

    count = (size_t)width * (size_t)height;
    if (count > MINIOS_PROCESS_MODEL_MAX_ELEMENTS ||
        count > SIZE_MAX / sizeof(int32_t)) {
        return 0;
    }
    bytes = count * sizeof(int32_t);
    offset = (sizeof(MiniOSProcessModelHeader) + alignof(int32_t) - 1u) &
             ~(alignof(int32_t) - 1u);
    if (bytes > SIZE_MAX - offset) {
        return 0;
    }

    *element_count = count;
    *weights_bytes = bytes;
    *weights_offset = offset;
    *region_size = offset + bytes;
    return 1;
}

static MiniOSProcessStatus process_errno_status(int error)
{
    if (error == ENOENT) {
        return MINIOS_PROCESS_NOT_FOUND;
    }
    if (error == ENOMEM) {
        return MINIOS_PROCESS_NO_MEMORY;
    }
    return MINIOS_PROCESS_IO_ERROR;
}

static void reset_region(MiniOSProcessRegion *region)
{
    region->model_fd = -1;
    region->progress_fd = -1;
    region->model_mapping = NULL;
    region->progress_mapping = NULL;
    region->model_mapping_size = 0u;
    region->progress_mapping_size = 0u;
    region->owns_regions = 0;
    region->model_name[0] = '\0';
    region->progress_name[0] = '\0';
}

void minios_process_region_init(MiniOSProcessRegion *region)
{
    if (region != NULL) {
        reset_region(region);
    }
}

static int map_header(int fd, size_t required_size, int protection,
                      void **mapping)
{
    void *address = mmap(NULL, required_size, protection, MAP_SHARED, fd, 0);
    if (address == MAP_FAILED) {
        return -1;
    }
    *mapping = address;
    return 0;
}

static MiniOSProcessStatus check_region_file(int fd,
                                             size_t header_size,
                                             size_t *file_size)
{
    struct stat status;

    if (fstat(fd, &status) != 0 || status.st_size < 0) {
        return MINIOS_PROCESS_IO_ERROR;
    }
    if ((uintmax_t)status.st_size < (uintmax_t)header_size) {
        return MINIOS_PROCESS_TRUNCATED;
    }
    if ((uintmax_t)status.st_size > (uintmax_t)SIZE_MAX) {
        return MINIOS_PROCESS_BAD_ABI;
    }
    *file_size = (size_t)status.st_size;
    return MINIOS_PROCESS_OK;
}

static MiniOSProcessStatus validate_model_header(
    const MiniOSProcessModelHeader *header,
    size_t file_size)
{
    size_t element_count;
    size_t weights_bytes;
    size_t weights_offset;
    size_t expected_size;

    if (header->common.magic != MINIOS_PROCESS_REGION_MAGIC ||
        header->common.abi_version != MINIOS_PROCESS_ABI_VERSION ||
        header->common.kind != MINIOS_PROCESS_MODEL_KIND ||
        header->common.header_size != sizeof(*header) ||
        header->common.run_id == 0u ||
        header->model_version == 0u) {
        return MINIOS_PROCESS_BAD_ABI;
    }
    if (!checked_model_size(header->width, header->height, &element_count,
                            &weights_bytes, &weights_offset,
                            &expected_size)) {
        return MINIOS_PROCESS_BAD_ABI;
    }
    if (header->common.region_size != file_size ||
        header->element_count != element_count ||
        header->weights_bytes != weights_bytes ||
        header->weights_offset != weights_offset ||
        expected_size != file_size) {
        return MINIOS_PROCESS_BAD_ABI;
    }
    return MINIOS_PROCESS_OK;
}

static MiniOSProcessStatus validate_progress_header(
    const MiniOSProcessProgressHeader *header,
    size_t file_size)
{
    size_t index;

    if (header->common.magic != MINIOS_PROCESS_REGION_MAGIC ||
        header->common.abi_version != MINIOS_PROCESS_ABI_VERSION ||
        header->common.kind != MINIOS_PROCESS_PROGRESS_KIND ||
        header->common.header_size != sizeof(*header) ||
        header->common.run_id == 0u ||
        header->actor_count == 0u ||
        header->actor_count > MINIOS_PROCESS_REGION_MAX_ACTORS ||
        header->update_capacity != MINIOS_PROCESS_UPDATE_CAPACITY ||
        header->common.region_size != file_size ||
        file_size != sizeof(*header)) {
        return MINIOS_PROCESS_BAD_ABI;
    }

    for (index = 0; index < header->actor_count; ++index) {
        if (header->actors[index].actor_id == 0u ||
            header->actors[index].total == 0u) {
            return MINIOS_PROCESS_BAD_ABI;
        }
    }
    return MINIOS_PROCESS_OK;
}

static MiniOSProcessStatus lock_progress(MiniOSProcessProgressHeader *header)
{
    int result = pthread_mutex_lock(&header->mutex);

    if (result == EOWNERDEAD) {
        /* Do not mark a possibly partial publication consistent. */
        (void)pthread_mutex_unlock(&header->mutex);
        return MINIOS_PROCESS_OWNER_DIED;
    }
    if (result == ENOTRECOVERABLE) {
        return MINIOS_PROCESS_OWNER_DIED;
    }
    return result == 0 ? MINIOS_PROCESS_OK : MINIOS_PROCESS_IO_ERROR;
}

static MiniOSProcessStatus unlock_progress(
    MiniOSProcessProgressHeader *header)
{
    return pthread_mutex_unlock(&header->mutex) == 0
        ? MINIOS_PROCESS_OK
        : MINIOS_PROCESS_IO_ERROR;
}

static MiniOSProcessStatus release_reserved_slot(
    MiniOSProcessProgressHeader *header,
    MiniOSProcessStatus status)
{
    return sem_post(&header->free_slots) == 0
        ? status
        : MINIOS_PROCESS_IO_ERROR;
}

static int progress_state_valid(
    const MiniOSProcessProgressHeader *header)
{
    size_t index;

    if (header->next_global_sequence == 0u ||
        header->next_read_sequence == 0u ||
        header->next_read_sequence > header->next_global_sequence ||
        header->next_global_sequence - header->next_read_sequence >
            MINIOS_PROCESS_UPDATE_CAPACITY) {
        return 0;
    }

    for (index = 0; index < header->actor_count; ++index) {
        const MiniOSProcessSharedProgress *record = &header->actors[index];
        if (record->actor_id == 0u || record->total == 0u ||
            record->completed > record->total ||
            record->state > MINIOS_PROCESS_STATE_FAILED) {
            return 0;
        }
        if (record->generation == 0u) {
            if (record->sequence != 0u ||
                record->state != MINIOS_PROCESS_STATE_EMPTY ||
                record->completed != 0u) {
                return 0;
            }
        } else if (record->sequence == 0u ||
                   record->state == MINIOS_PROCESS_STATE_EMPTY ||
                   (record->state == MINIOS_PROCESS_STATE_DONE &&
                    record->completed != record->total)) {
            return 0;
        }
    }
    return 1;
}

static int semaphore_wait(sem_t *semaphore, uint32_t timeout_ms)
{
    struct timespec deadline;
    int result;

    if (timeout_ms == 0u) {
        do {
            result = sem_trywait(semaphore);
        } while (result != 0 && errno == EINTR);
        return result;
    }

    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        errno = EIO;
        return -1;
    }
    deadline.tv_sec += (time_t)(timeout_ms / 1000u);
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    do {
        result = sem_timedwait(semaphore, &deadline);
    } while (result != 0 && errno == EINTR);
    return result;
}

static MiniOSProcessStatus wait_semaphore(sem_t *semaphore,
                                          uint32_t timeout_ms,
                                          int is_slot)
{
    if (semaphore_wait(semaphore, timeout_ms) == 0) {
        return MINIOS_PROCESS_OK;
    }
    if (errno == EAGAIN && is_slot) {
        return MINIOS_PROCESS_BACKPRESSURE;
    }
    if (errno == ETIMEDOUT || (errno == EAGAIN && !is_slot)) {
        return MINIOS_PROCESS_TIMEOUT;
    }
    return MINIOS_PROCESS_IO_ERROR;
}

static MiniOSProcessStatus open_model_mapping(const char *name,
                                             int flags,
                                             int *fd_out,
                                             void **mapping_out,
                                             size_t *size_out,
                                             MiniOSProcessModelHeader *copy)
{
    MiniOSProcessStatus result;
    MiniOSProcessModelHeader *header;
    void *header_mapping = NULL;
    size_t file_size;
    int fd = shm_open(name, flags, 0);

    if (fd < 0) {
        return process_errno_status(errno);
    }
    result = check_region_file(fd, sizeof(*header), &file_size);
    if (result != MINIOS_PROCESS_OK) {
        close(fd);
        return result;
    }
    if (map_header(fd, sizeof(*header),
                   (flags & O_ACCMODE) == O_RDONLY
                       ? PROT_READ : PROT_READ | PROT_WRITE,
                   &header_mapping) != 0) {
        result = process_errno_status(errno);
        close(fd);
        return result;
    }

    header = (MiniOSProcessModelHeader *)header_mapping;
    result = validate_model_header(header, file_size);
    if (result == MINIOS_PROCESS_OK) {
        *copy = *header;
    }
    (void)munmap(header_mapping, sizeof(*header));
    if (result != MINIOS_PROCESS_OK) {
        close(fd);
        return result;
    }

    {
        int protection = (flags & O_ACCMODE) == O_RDONLY
            ? PROT_READ
            : PROT_READ | PROT_WRITE;
        void *mapping = mmap(NULL, file_size, protection, MAP_SHARED, fd, 0);
        if (mapping == MAP_FAILED) {
            result = process_errno_status(errno);
            close(fd);
            return result;
        }
        *fd_out = fd;
        *mapping_out = mapping;
        *size_out = file_size;
    }
    return MINIOS_PROCESS_OK;
}

static MiniOSProcessStatus open_progress_mapping(
    const char *name,
    int *fd_out,
    void **mapping_out,
    size_t *size_out,
    MiniOSProcessProgressHeader **header_out)
{
    MiniOSProcessStatus result;
    MiniOSProcessProgressHeader *header;
    void *header_mapping = NULL;
    size_t file_size;
    int fd = shm_open(name, O_RDWR, 0);

    if (fd < 0) {
        return process_errno_status(errno);
    }
    result = check_region_file(fd, sizeof(*header), &file_size);
    if (result != MINIOS_PROCESS_OK) {
        close(fd);
        return result;
    }
    if (map_header(fd, sizeof(*header), PROT_READ | PROT_WRITE,
                   &header_mapping) != 0) {
        result = process_errno_status(errno);
        close(fd);
        return result;
    }
    header = (MiniOSProcessProgressHeader *)header_mapping;
    result = validate_progress_header(header, file_size);
    (void)munmap(header_mapping, sizeof(*header));
    if (result != MINIOS_PROCESS_OK) {
        close(fd);
        return result;
    }

    {
        void *mapping = mmap(NULL, file_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, 0);
        if (mapping == MAP_FAILED) {
            result = process_errno_status(errno);
            close(fd);
            return result;
        }
        *fd_out = fd;
        *mapping_out = mapping;
        *size_out = file_size;
        *header_out = (MiniOSProcessProgressHeader *)mapping;
    }
    return MINIOS_PROCESS_OK;
}

static void close_mappings(MiniOSProcessRegion *region)
{
    if (region->model_mapping != NULL) {
        (void)munmap(region->model_mapping, region->model_mapping_size);
    }
    if (region->progress_mapping != NULL) {
        (void)munmap(region->progress_mapping,
                     region->progress_mapping_size);
    }
    if (region->model_fd >= 0) {
        (void)close(region->model_fd);
    }
    if (region->progress_fd >= 0) {
        (void)close(region->progress_fd);
    }
    reset_region(region);
}

static int initialize_progress_sync(MiniOSProcessProgressHeader *header)
{
    pthread_mutexattr_t mutex_attributes;
    int attributes_initialized = 0;
    int mutex_initialized = 0;
    int updates_initialized = 0;
    int slots_initialized = 0;
    int result;

    result = pthread_mutexattr_init(&mutex_attributes);
    if (result != 0) {
        return result;
    }
    attributes_initialized = 1;
    result = pthread_mutexattr_setpshared(&mutex_attributes,
                                          PTHREAD_PROCESS_SHARED);
    if (result == 0) {
        result = pthread_mutexattr_setrobust(&mutex_attributes,
                                             PTHREAD_MUTEX_ROBUST);
    }
    if (result == 0) {
        result = pthread_mutex_init(&header->mutex, &mutex_attributes);
    }
    if (result == 0) {
        mutex_initialized = 1;
        result = sem_init(&header->updates_ready, 1, 0u);
    }
    if (result == 0) {
        updates_initialized = 1;
        result = sem_init(&header->free_slots, 1,
                          MINIOS_PROCESS_UPDATE_CAPACITY);
        slots_initialized = result == 0;
    }
    if (attributes_initialized) {
        (void)pthread_mutexattr_destroy(&mutex_attributes);
    }
    if (result != 0) {
        if (slots_initialized) {
            (void)sem_destroy(&header->free_slots);
        }
        if (updates_initialized) {
            (void)sem_destroy(&header->updates_ready);
        }
        if (mutex_initialized) {
            (void)pthread_mutex_destroy(&header->mutex);
        }
    }
    return result;
}

MiniOSProcessStatus minios_process_region_create(
    MiniOSProcessRegion *region,
    const MiniOSProcessRegionSpec *spec)
{
    MiniOSProcessStatus result = MINIOS_PROCESS_BAD_ARGUMENT;
    MiniOSProcessModelHeader model_header;
    MiniOSProcessProgressHeader *progress_header = NULL;
    void *model_write_mapping = NULL;
    size_t element_count;
    size_t weights_bytes;
    size_t weights_offset;
    size_t model_size;
    size_t index;
    int model_created = 0;
    int progress_created = 0;
    int model_write_fd = -1;
    int progress_fd = -1;
    void *progress_mapping = NULL;

    if (region == NULL) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    minios_process_region_init(region);
    if (spec == NULL || !valid_name(spec->model_name) ||
        !valid_name(spec->progress_name) ||
        strcmp(spec->model_name, spec->progress_name) == 0 ||
        spec->run_id == 0u || spec->model_version == 0u ||
        spec->actor_ids == NULL || spec->actor_totals == NULL ||
        spec->actor_count == 0u ||
        spec->actor_count > MINIOS_PROCESS_REGION_MAX_ACTORS ||
        !checked_model_size(spec->width, spec->height, &element_count,
                            &weights_bytes, &weights_offset, &model_size)) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    for (index = 0; index < spec->actor_count; ++index) {
        size_t prior;
        if (spec->actor_ids[index] == 0u ||
            spec->actor_totals[index] == 0u) {
            return MINIOS_PROCESS_BAD_ARGUMENT;
        }
        for (prior = 0; prior < index; ++prior) {
            if (spec->actor_ids[prior] == spec->actor_ids[index]) {
                return MINIOS_PROCESS_BAD_ARGUMENT;
            }
        }
    }

    if (!copy_name(region->model_name, spec->model_name) ||
        !copy_name(region->progress_name, spec->progress_name)) {
        minios_process_region_init(region);
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }

    model_write_fd = shm_open(spec->model_name, O_CREAT | O_EXCL | O_RDWR,
                              S_IRUSR | S_IWUSR);
    if (model_write_fd < 0) {
        result = process_errno_status(errno);
        goto failed;
    }
    model_created = 1;
    if (ftruncate(model_write_fd, (off_t)model_size) != 0) {
        result = process_errno_status(errno);
        goto failed;
    }
    model_write_mapping = mmap(NULL, model_size, PROT_READ | PROT_WRITE,
                               MAP_SHARED, model_write_fd, 0);
    if (model_write_mapping == MAP_FAILED) {
        model_write_mapping = NULL;
        result = process_errno_status(errno);
        goto failed;
    }
    memset(model_write_mapping, 0, model_size);
    memset(&model_header, 0, sizeof(model_header));
    model_header.common.abi_version = MINIOS_PROCESS_ABI_VERSION;
    model_header.common.kind = MINIOS_PROCESS_MODEL_KIND;
    model_header.common.header_size = (uint32_t)sizeof(model_header);
    model_header.common.region_size = model_size;
    model_header.common.run_id = spec->run_id;
    model_header.model_version = spec->model_version;
    model_header.width = spec->width;
    model_header.height = spec->height;
    model_header.element_count = element_count;
    model_header.weights_offset = weights_offset;
    model_header.weights_bytes = weights_bytes;
    memcpy(model_write_mapping, &model_header, sizeof(model_header));
    for (index = 0; index < element_count; ++index) {
        uint32_t row = (uint32_t)(index / spec->width);
        uint32_t column = (uint32_t)(index % spec->width);
        int32_t value = (int32_t)(((UINT64_C(17) * row +
                                    UINT64_C(31) * column) % 101u)) - 50;
        ((int32_t *)((unsigned char *)model_write_mapping +
                     weights_offset))[index] = value;
    }
    ((MiniOSProcessModelHeader *)model_write_mapping)->common.magic =
        MINIOS_PROCESS_REGION_MAGIC;
    (void)munmap(model_write_mapping, model_size);
    model_write_mapping = NULL;
    (void)close(model_write_fd);
    model_write_fd = -1;

    result = open_model_mapping(spec->model_name, O_RDONLY,
                                &region->model_fd, &region->model_mapping,
                                &region->model_mapping_size, &model_header);
    if (result != MINIOS_PROCESS_OK) {
        goto failed;
    }

    progress_fd = shm_open(spec->progress_name,
                           O_CREAT | O_EXCL | O_RDWR,
                           S_IRUSR | S_IWUSR);
    if (progress_fd < 0) {
        result = process_errno_status(errno);
        goto failed;
    }
    progress_created = 1;
    if (ftruncate(progress_fd,
                  (off_t)sizeof(MiniOSProcessProgressHeader)) != 0) {
        result = process_errno_status(errno);
        goto failed;
    }
    progress_mapping = mmap(NULL, sizeof(MiniOSProcessProgressHeader),
                            PROT_READ | PROT_WRITE, MAP_SHARED, progress_fd, 0);
    if (progress_mapping == MAP_FAILED) {
        progress_mapping = NULL;
        result = process_errno_status(errno);
        goto failed;
    }
    memset(progress_mapping, 0, sizeof(MiniOSProcessProgressHeader));
    progress_header = (MiniOSProcessProgressHeader *)progress_mapping;
    progress_header->common.abi_version = MINIOS_PROCESS_ABI_VERSION;
    progress_header->common.kind = MINIOS_PROCESS_PROGRESS_KIND;
    progress_header->common.header_size =
        (uint32_t)sizeof(MiniOSProcessProgressHeader);
    progress_header->common.region_size =
        sizeof(MiniOSProcessProgressHeader);
    progress_header->common.run_id = spec->run_id;
    progress_header->model_run_id = spec->run_id;
    progress_header->actor_count = (uint32_t)spec->actor_count;
    progress_header->update_capacity = MINIOS_PROCESS_UPDATE_CAPACITY;
    progress_header->next_global_sequence = 1u;
    progress_header->next_read_sequence = 1u;
    for (index = 0; index < spec->actor_count; ++index) {
        progress_header->actors[index].actor_id = spec->actor_ids[index];
        progress_header->actors[index].total = spec->actor_totals[index];
    }
    if (initialize_progress_sync(progress_header) != 0) {
        result = MINIOS_PROCESS_IO_ERROR;
        goto failed;
    }
    progress_header->common.magic = MINIOS_PROCESS_REGION_MAGIC;

    region->progress_fd = progress_fd;
    region->progress_mapping = progress_mapping;
    region->progress_mapping_size =
        sizeof(MiniOSProcessProgressHeader);
    region->owns_regions = 1;
    return MINIOS_PROCESS_OK;

failed:
    if (model_write_mapping != NULL) {
        (void)munmap(model_write_mapping, model_size);
    }
    if (progress_mapping != NULL) {
        (void)munmap(progress_mapping,
                     sizeof(MiniOSProcessProgressHeader));
    }
    if (progress_fd >= 0) {
        (void)close(progress_fd);
    }
    if (model_write_fd >= 0) {
        (void)close(model_write_fd);
    }
    close_mappings(region);
    if (progress_created) {
        (void)shm_unlink(spec->progress_name);
    }
    if (model_created) {
        (void)shm_unlink(spec->model_name);
    }
    return result;
}

MiniOSProcessStatus minios_process_region_attach(
    MiniOSProcessRegion *region,
    const char *model_name,
    const char *progress_name)
{
    MiniOSProcessStatus result;
    MiniOSProcessModelHeader model_header;
    MiniOSProcessProgressHeader *progress_header = NULL;

    if (region == NULL) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    minios_process_region_init(region);
    if (!valid_name(model_name) || !valid_name(progress_name) ||
        strcmp(model_name, progress_name) == 0 ||
        !copy_name(region->model_name, model_name) ||
        !copy_name(region->progress_name, progress_name)) {
        minios_process_region_init(region);
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }

    result = open_model_mapping(model_name, O_RDONLY,
                                &region->model_fd, &region->model_mapping,
                                &region->model_mapping_size, &model_header);
    if (result != MINIOS_PROCESS_OK) {
        close_mappings(region);
        return result;
    }
    result = open_progress_mapping(progress_name, &region->progress_fd,
                                   &region->progress_mapping,
                                   &region->progress_mapping_size,
                                   &progress_header);
    if (result != MINIOS_PROCESS_OK) {
        close_mappings(region);
        return result;
    }
    if (progress_header->common.run_id != model_header.common.run_id ||
        progress_header->model_run_id != model_header.common.run_id) {
        close_mappings(region);
        return MINIOS_PROCESS_BAD_ABI;
    }
    return MINIOS_PROCESS_OK;
}

MiniOSProcessStatus minios_process_region_detach(
    MiniOSProcessRegion *region)
{
    if (region == NULL || region->model_fd < 0 ||
        region->progress_fd < 0 || region->owns_regions) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    close_mappings(region);
    return MINIOS_PROCESS_OK;
}

MiniOSProcessStatus minios_process_region_destroy(
    MiniOSProcessRegion *region)
{
    int unlink_failed = 0;
    MiniOSProcessProgressHeader *header;

    if (region == NULL || !region->owns_regions) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    header = (MiniOSProcessProgressHeader *)region->progress_mapping;
    if (header != NULL) {
        if (pthread_mutex_destroy(&header->mutex) != 0) {
            unlink_failed = 1;
        }
        if (sem_destroy(&header->updates_ready) != 0) {
            unlink_failed = 1;
        }
        if (sem_destroy(&header->free_slots) != 0) {
            unlink_failed = 1;
        }
    }
    if (shm_unlink(region->model_name) != 0 && errno != ENOENT) {
        unlink_failed = 1;
    }
    if (shm_unlink(region->progress_name) != 0 && errno != ENOENT) {
        unlink_failed = 1;
    }
    close_mappings(region);
    return unlink_failed ? MINIOS_PROCESS_IO_ERROR : MINIOS_PROCESS_OK;
}

const char *minios_process_region_model_name(
    const MiniOSProcessRegion *region)
{
    return region != NULL && region->model_name[0] != '\0'
        ? region->model_name
        : NULL;
}

const char *minios_process_region_progress_name(
    const MiniOSProcessRegion *region)
{
    return region != NULL && region->progress_name[0] != '\0'
        ? region->progress_name
        : NULL;
}

MiniOSProcessStatus minios_process_model_view(
    const MiniOSProcessRegion *region,
    MiniOSProcessModelView *out)
{
    const MiniOSProcessModelHeader *header;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (region == NULL || out == NULL || region->model_mapping == NULL) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    header = (const MiniOSProcessModelHeader *)region->model_mapping;
    out->model_version = header->model_version;
    out->width = header->width;
    out->height = header->height;
    out->element_count = (size_t)header->element_count;
    out->byte_length = (size_t)header->weights_bytes;
    out->weights = (const int32_t *)((const unsigned char *)
        region->model_mapping + header->weights_offset);
    return MINIOS_PROCESS_OK;
}

MiniOSProcessStatus minios_process_model_read(
    const MiniOSProcessRegion *region,
    size_t offset,
    size_t length,
    const void **out)
{
    const MiniOSProcessModelHeader *header;
    size_t model_bytes;

    if (out == NULL) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    *out = NULL;
    if (region == NULL || region->model_mapping == NULL) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    header = (const MiniOSProcessModelHeader *)region->model_mapping;
    model_bytes = (size_t)header->weights_bytes;
    if (offset > model_bytes || length > model_bytes - offset) {
        return MINIOS_PROCESS_BAD_RANGE;
    }
    *out = (const unsigned char *)region->model_mapping +
           (size_t)header->weights_offset + offset;
    return MINIOS_PROCESS_OK;
}

static MiniOSProcessSharedProgress *find_actor(
    MiniOSProcessProgressHeader *header,
    uint32_t actor_id)
{
    size_t index;

    for (index = 0; index < header->actor_count; ++index) {
        if (header->actors[index].actor_id == actor_id) {
            return &header->actors[index];
        }
    }
    return NULL;
}

static void copy_progress(const MiniOSProcessSharedProgress *source,
                          MiniOSProcessProgress *destination)
{
    destination->actor_id = source->actor_id;
    destination->generation = source->generation;
    destination->state = (MiniOSProcessState)source->state;
    destination->completed = source->completed;
    destination->total = source->total;
    destination->sequence = source->sequence;
    destination->result = source->result;
}

MiniOSProcessStatus minios_process_progress_snapshot(
    MiniOSProcessRegion *region,
    uint32_t actor_id,
    MiniOSProcessProgress *out)
{
    MiniOSProcessStatus result;
    MiniOSProcessProgressHeader *header;
    MiniOSProcessSharedProgress *record;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (region == NULL || out == NULL || region->progress_mapping == NULL ||
        actor_id == 0u) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    header = (MiniOSProcessProgressHeader *)region->progress_mapping;
    result = lock_progress(header);
    if (result != MINIOS_PROCESS_OK) {
        return result;
    }
    if (!progress_state_valid(header)) {
        (void)unlock_progress(header);
        return MINIOS_PROCESS_UPDATE_CORRUPT;
    }
    record = find_actor(header, actor_id);
    if (record == NULL) {
        (void)unlock_progress(header);
        return MINIOS_PROCESS_NOT_FOUND;
    }
    copy_progress(record, out);
    return unlock_progress(header);
}

static MiniOSProcessStatus validate_publication(
    const MiniOSProcessSharedProgress *record,
    uint32_t generation,
    uint64_t sequence,
    MiniOSProcessState state,
    uint32_t completed,
    uint32_t total)
{
    if (generation == 0u || sequence == 0u || total == 0u ||
        total != record->total || completed > total ||
        (state != MINIOS_PROCESS_STATE_RUNNING &&
         state != MINIOS_PROCESS_STATE_DONE &&
         state != MINIOS_PROCESS_STATE_FAILED) ||
        completed < record->completed ||
        (state == MINIOS_PROCESS_STATE_DONE && completed != total)) {
        return MINIOS_PROCESS_BAD_PROGRESS;
    }
    if (record->sequence == UINT64_MAX ||
        sequence != record->sequence + 1u) {
        return MINIOS_PROCESS_BAD_SEQUENCE;
    }
    if (record->generation == 0u) {
        return generation == 1u && state == MINIOS_PROCESS_STATE_RUNNING
            ? MINIOS_PROCESS_OK
            : MINIOS_PROCESS_STALE_GENERATION;
    }
    if (generation == record->generation) {
        if (record->state != MINIOS_PROCESS_STATE_RUNNING) {
            return MINIOS_PROCESS_BAD_PROGRESS;
        }
        return MINIOS_PROCESS_OK;
    }
    if (record->generation == UINT32_MAX ||
        generation != record->generation + 1u ||
        completed != record->completed ||
        state != MINIOS_PROCESS_STATE_RUNNING) {
        return MINIOS_PROCESS_STALE_GENERATION;
    }
    return MINIOS_PROCESS_OK;
}

MiniOSProcessStatus minios_process_progress_publish(
    MiniOSProcessRegion *region,
    uint32_t actor_id,
    uint32_t generation,
    uint64_t sequence,
    MiniOSProcessState state,
    uint32_t completed,
    uint32_t total,
    uint64_t result_value,
    uint32_t timeout_ms)
{
    MiniOSProcessStatus result;
    MiniOSProcessProgressHeader *header;
    MiniOSProcessSharedProgress *record;
    MiniOSProcessUpdate *update;
    uint64_t global_sequence;

    if (region == NULL || region->progress_mapping == NULL ||
        actor_id == 0u) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    header = (MiniOSProcessProgressHeader *)region->progress_mapping;
    result = wait_semaphore(&header->free_slots, timeout_ms, 1);
    if (result != MINIOS_PROCESS_OK) {
        return result;
    }
    result = lock_progress(header);
    if (result != MINIOS_PROCESS_OK) {
        return release_reserved_slot(header, result);
    }
    if (!progress_state_valid(header)) {
        MiniOSProcessStatus unlock_status = unlock_progress(header);
        return release_reserved_slot(
            header, unlock_status == MINIOS_PROCESS_OK
                        ? MINIOS_PROCESS_UPDATE_CORRUPT : unlock_status);
    }

    record = find_actor(header, actor_id);
    if (record == NULL) {
        result = unlock_progress(header);
        return release_reserved_slot(
            header, result == MINIOS_PROCESS_OK
                        ? MINIOS_PROCESS_NOT_FOUND : result);
    }
    result = validate_publication(record, generation, sequence, state,
                                 completed, total);
    if (result != MINIOS_PROCESS_OK) {
        MiniOSProcessStatus unlock_status = unlock_progress(header);
        return release_reserved_slot(
            header, unlock_status == MINIOS_PROCESS_OK ? result
                                                       : unlock_status);
    }
    global_sequence = header->next_global_sequence;
    if (global_sequence == UINT64_MAX) {
        MiniOSProcessStatus unlock_status = unlock_progress(header);
        return release_reserved_slot(
            header, unlock_status == MINIOS_PROCESS_OK
                        ? MINIOS_PROCESS_BAD_SEQUENCE : unlock_status);
    }
    update = &header->updates[(global_sequence - 1u) %
                              MINIOS_PROCESS_UPDATE_CAPACITY];
    update->global_sequence = global_sequence;
    update->progress.actor_id = actor_id;
    update->progress.generation = generation;
    update->progress.state = state;
    update->progress.completed = completed;
    update->progress.total = total;
    update->progress.sequence = sequence;
    update->progress.result = result_value;

    record->generation = generation;
    record->state = (uint32_t)state;
    record->completed = completed;
    record->sequence = sequence;
    record->result = result_value;
    header->next_global_sequence = global_sequence + 1u;
    result = unlock_progress(header);
    if (result != MINIOS_PROCESS_OK) {
        return result;
    }
    if (sem_post(&header->updates_ready) != 0) {
        return MINIOS_PROCESS_IO_ERROR;
    }
    return MINIOS_PROCESS_OK;
}

MiniOSProcessStatus minios_process_wait_update(
    MiniOSProcessRegion *region,
    MiniOSProcessUpdate *out,
    uint32_t timeout_ms)
{
    MiniOSProcessStatus result;
    MiniOSProcessProgressHeader *header;
    MiniOSProcessUpdate *stored;
    uint64_t read_sequence;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (region == NULL || out == NULL || region->progress_mapping == NULL) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    header = (MiniOSProcessProgressHeader *)region->progress_mapping;
    result = wait_semaphore(&header->updates_ready, timeout_ms, 0);
    if (result != MINIOS_PROCESS_OK) {
        return result;
    }
    result = lock_progress(header);
    if (result != MINIOS_PROCESS_OK) {
        return result;
    }
    if (!progress_state_valid(header)) {
        (void)unlock_progress(header);
        return MINIOS_PROCESS_UPDATE_CORRUPT;
    }
    read_sequence = header->next_read_sequence;
    if (read_sequence >= header->next_global_sequence) {
        (void)unlock_progress(header);
        return MINIOS_PROCESS_UPDATE_CORRUPT;
    }
    stored = &header->updates[(read_sequence - 1u) %
                              MINIOS_PROCESS_UPDATE_CAPACITY];
    if (stored->global_sequence != read_sequence) {
        (void)unlock_progress(header);
        return MINIOS_PROCESS_UPDATE_CORRUPT;
    }
    *out = *stored;
    header->next_read_sequence = read_sequence + 1u;
    result = unlock_progress(header);
    if (result != MINIOS_PROCESS_OK) {
        return result;
    }
    if (sem_post(&header->free_slots) != 0) {
        return MINIOS_PROCESS_IO_ERROR;
    }
    return MINIOS_PROCESS_OK;
}

#ifdef MINIOS_PROCESS_REGION_TESTING
void minios_process_region_test_abandon_control_lock(
    MiniOSProcessRegion *region)
{
    MiniOSProcessProgressHeader *header =
        (MiniOSProcessProgressHeader *)region->progress_mapping;
    if (pthread_mutex_lock(&header->mutex) == 0) {
        _exit(0);
    }
    _exit(2);
}
#endif
