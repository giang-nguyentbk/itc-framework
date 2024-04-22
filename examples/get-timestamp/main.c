#include <stdio.h>
#include <time.h>
#include <sys/time.h>

// void get_time1(void)
// {
// 	char log_buff[50];

// 	int time_len = 0, n;
// 	struct tm *tm_info;
// 	struct timespec ts;
// 	struct timeval tv;

// 	clock_gettime(CLOCK_MONOTONIC, &ts);
// 	gettimeofday(&tv, NULL)
// 	tm_info = localtime(&ts.tv_sec);
// 	time_len+=strftime(log_buff, sizeof log_buff, "%Y-%m-%d %H:%M:%S", tm_info);
// 	time_len+=snprintf(log_buff+time_len,sizeof log_buff-time_len,".%09ld ", ts.tv_nsec);

// 	printf("Timestamp 1: %s\n", log_buff);
// }

void get_time2(void)
{
	time_t timer;
	char timestamp[26];
	struct tm* tm_info;
	timer = time(NULL);
	tm_info = localtime(&timer);
	strftime(timestamp, 26, "%Y-%m-%d %H:%M:%S", tm_info);

	printf("Timestamp 1: %s\n", timestamp);
}

void get_time3(void)
{
	char timeStringEnd[50];
	struct timespec tv;
	clock_gettime(CLOCK_REALTIME, &tv);
	struct tm *timePointerEnd = localtime(&tv.tv_sec);
	size_t nbytes = strftime(timeStringEnd, sizeof(timeStringEnd), "%Y-%m-%d %H:%M:%S", timePointerEnd);
	snprintf(timeStringEnd + nbytes, sizeof(timeStringEnd) - nbytes,
		"%.9ld", tv.tv_nsec);
	puts(timeStringEnd);
}

int main(void)
{
	
	// get_time1();

	get_time2();

	get_time3();

	return 0;
}