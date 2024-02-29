/*
 * header file to be used by applications.
 */

int printu(const char *s, ...);
int exit(int code);
void* naive_malloc();
void naive_free(void* va);
int fork();
void yield();

// added in lab3_challenge2
int sem_new(int v);
void sem_P(int sem_id);
void sem_V(int sem_id);