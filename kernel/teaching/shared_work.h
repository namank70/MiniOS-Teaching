#ifndef TEACHING_SHARED_WORK_H
#define TEACHING_SHARED_WORK_H
/* Shell owns commands/observation and all console writes. */
int shared_work_command(const char *line);
int shared_work_observe(void);
void shared_work_progress(void);
void shared_worker_a(void);
void shared_worker_b(void);
#endif
