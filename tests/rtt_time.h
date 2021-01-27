#ifndef RTT_TIME
#define RTT_TIME

/* this struct must have the same size on arm and arm64 */
typedef struct {
	unsigned long long tv_sec;
	unsigned long long tv_nsec;
} time_struct_64;

#endif /* RTT_TIME */
