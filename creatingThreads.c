
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <inttypes.h>
#include <signal.h>
#include <errno.h>

#define	MAX_THREAD_COUNT		3

#define LOW_THREAD_PRIORITY		60

#define STACK_SIZE				0x400000
#define	MAX_TASK_COUNT			5

//define error handling later on
#define handle_error_en(en, msg) \
        do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)
			
/*****************************************************************************************/
pthread_mutex_t		g_DisplayMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t 	g_SignalMutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct{
	int					threadCount;
	pthread_t			threadId;
	int					threadPolicy;
	int					threadPri;
	struct sched_param 	param;	
	long				processTime;
	int64_t				timeStamp[MAX_TASK_COUNT+1];
	time_t				startTime;
	time_t				endTime;
	int 				sig;
	int 				wakeups_missed;
	timer_t 			timer_id;
	int 				timer_Period;
    sigset_t			timer_signal;	
} ThreadArgs;

ThreadArgs g_ThreadArgs[MAX_THREAD_COUNT];

/*****************************************************************************************/

void InitThreadArgs(void)
{
	for(int i=0;i<MAX_THREAD_COUNT;i++) 
	{
		g_ThreadArgs[i].threadCount = 0;
		g_ThreadArgs[i].threadId = 0;
		g_ThreadArgs[i].threadPri = 0;
		g_ThreadArgs[i].processTime =0;
		for(int y=0; y<MAX_TASK_COUNT+1; y++)
		{
			g_ThreadArgs[i].timeStamp[y] = 0;
		}
	}	
	
	pthread_mutex_init ( &g_DisplayMutex, NULL);
}

/*****************************************************************************************/

void DisplayThreadSchdAttributes( pthread_t threadID, int policy, int priority )
{
	 
	   printf("\nDisplayThreadSchdAttributes:\n        threadID = 0x%lx\n        policy = %s\n        priority = %d\n", 
			threadID,
		   (policy == SCHED_FIFO)  ? "SCHED_FIFO" :
		   (policy == SCHED_RR)	   ? "SCHED_RR" :
		   (policy == SCHED_OTHER) ? "SCHED_OTHER" :
		   "???",
		   priority);
}

/*****************************************************************************************/

void DisplayThreadArgs(ThreadArgs*	myThreadArg)
{
	int i,y;
	
	pthread_mutex_lock( &g_DisplayMutex );
	if( myThreadArg )
	{
			DisplayThreadSchdAttributes(myThreadArg->threadId, myThreadArg->threadPolicy, myThreadArg->threadPri);
			printf("        startTime = %s", ctime(&myThreadArg->startTime));
			printf("        endTime = %s", ctime(&myThreadArg->endTime));
			printf("        TimeStamp [%"PRId64"]\n", myThreadArg->timeStamp[0] );

			for(int y=1; y<MAX_TASK_COUNT+1; y++)
			{
				printf("        TimeStamp [%"PRId64"]   Delta [%"PRId64"]us     Jitter[%"PRId64"]us\n", myThreadArg->timeStamp[y], 
							(myThreadArg->timeStamp[y]-myThreadArg->timeStamp[y-1]),
							(myThreadArg->timeStamp[y]-myThreadArg->timeStamp[y-1]-myThreadArg->timer_Period));
			}	
			printf("        Wakeups missed: %d\n", myThreadArg->wakeups_missed);
	
	}
	pthread_mutex_unlock( &g_DisplayMutex );	
}

/*****************************************************************************************/

int CreateAndArmTimer(int unsigned period, ThreadArgs* info)
{
    struct sigevent mySignalEvent;
	struct itimerspec timerSpec;
	int ret;

	//Creating timer
	mySignalEvent.sigev_notify = SIGEV_SIGNAL;
	mySignalEvent.sigev_signo = info->sig;
	mySignalEvent.sigev_value.sival_ptr = (void*)&(info->timer_id);
	ret = timer_create(CLOCK_MONOTONIC, &mySignalEvent, &info->timer_id);
	if (ret != 0)
		handle_error_en(ret, "timer_create");




	//Arming timer
	int seconds = period / 1000000;
	int nanoseconds = (period - (seconds * 1000000)) * 1000;
	timerSpec.it_interval.tv_sec = seconds;
	timerSpec.it_interval.tv_nsec = nanoseconds;
	timerSpec.it_value.tv_sec = seconds;
	timerSpec.it_value.tv_nsec = nanoseconds;
	ret = timer_settime(info->timer_id, 0, &timerSpec, NULL);
	if (ret != 0)
		handle_error_en(ret, "timer_settime");


}

	

/*****************************************************************************************/

static void wait_period (ThreadArgs *info)
{
	int tempSig;

	int ret;
	ret = sigwait(&info->timer_signal, &tempSig);
	if (ret != 0)
		handle_error_en(ret, "sigwait");

	info->wakeups_missed += timer_getoverrun(info->timer_id);




}

/*****************************************************************************************/

void* threadFunction(void *arg)
{
	ThreadArgs*	myThreadArg;
	struct timeval	t1;
	struct timespec tms;
	int y, retVal;
  
	myThreadArg = (ThreadArgs*)arg;

	if( myThreadArg->threadId != pthread_self() )
	{
		printf("mismatched thread Ids... exiting...\n");
		pthread_exit(arg);
	}
	else
	{
		retVal = pthread_setschedparam(pthread_self(), myThreadArg->threadPolicy, &myThreadArg->param);		//SCHED_FIFO, SCHED_RR, SCHED_OTHER
		if(retVal != 0){
			handle_error_en(retVal, "pthread_setschedparam");
		}
		myThreadArg->processTime = 0;
	}

	sigemptyset(&myThreadArg->timer_signal);
	sigaddset(&myThreadArg->timer_signal, myThreadArg->sig);
	sigprocmask(SIG_BLOCK, &myThreadArg->timer_signal, NULL);

	//TODO: Call "CreateAndArmTimer(myThreadArg->timer_Period, myThreadArg);"

	pthread_mutex_lock(&g_SignalMutex);
	CreateAndArmTimer(myThreadArg->timer_Period, myThreadArg);


	myThreadArg->startTime = time(NULL);	

	//TODO: In a loop call "wait_period(myThreadArg);" taking a timestamp before and after using "clock_gettime(CLOCK_REALTIME, &tms);"	
	clock_gettime(CLOCK_REALTIME, &tms);
	myThreadArg->timeStamp[0] = tms.tv_sec *1000000;
	myThreadArg->timeStamp[0] += tms.tv_nsec/1000;
	if(tms.tv_nsec % 1000 >= 500 ) myThreadArg->timeStamp[0]++;

	for (int i = 0; i < MAX_TASK_COUNT; i++)
	{
		wait_period(myThreadArg);


		clock_gettime(CLOCK_REALTIME, &tms);
		myThreadArg->timeStamp[i+1] = tms.tv_sec *1000000;
		myThreadArg->timeStamp[i+1] += tms.tv_nsec/1000;
		if(tms.tv_nsec % 1000 >= 500 ) myThreadArg->timeStamp[i+1]++;
	}

	
	time_t tmp;
	tmp = time(NULL);
	myThreadArg->endTime = time(NULL);

	pthread_mutex_unlock(&g_SignalMutex);

	DisplayThreadArgs(myThreadArg);
	
	pthread_exit(NULL);
	
	
	return NULL;
}

/*****************************************************************************************/

int main (int argc, char *argv[]) 
{
	int threadCount = 0;
    int err, i;
	int fifoPri = 60;
	int period = 1;
	int retVal;

	sigset_t alarm_sig;

	sigemptyset (&alarm_sig);
	for (i = SIGRTMIN; i <= SIGRTMAX; i++)
		sigaddset (&alarm_sig, i);
	sigprocmask (SIG_BLOCK, &alarm_sig, NULL);



	
	
	InitThreadArgs();

   for (int i = 0; i < MAX_THREAD_COUNT; i ++)
   {
	  g_ThreadArgs[i].threadCount = i+1;
      g_ThreadArgs[i].threadPolicy = SCHED_FIFO;
      g_ThreadArgs[i].param.sched_priority = fifoPri;
	  g_ThreadArgs[i].threadPri = fifoPri;
	  g_ThreadArgs[i].timer_Period = (period << i)*1000000;
	  g_ThreadArgs[i].sig = SIGRTMIN + i;

      retVal = pthread_create(&g_ThreadArgs[i].threadId, NULL, threadFunction, &g_ThreadArgs[i]);
	  if(retVal != 0)
	  {
		handle_error_en(retVal, "pthread_create");
	  }	  
	  
   }

	for(int i = 0; i < MAX_THREAD_COUNT; i++)
	{
      pthread_join(g_ThreadArgs[i].threadId, NULL);
    }   
   
	printf("Main thread is exiting\n");
	
    return 0;	
}

/*****************************************************************************************/


/*

ANALYSIS OF JITTER OUTPUT

	The jitter seen in this program comes from the fact that the CPU that is handleing
these threads can't focus on just one process for too long, it uses the scheduler to 
move processes in and out of the CPU as needed. Because of this, the timer set up by
my program could be slightly off in timing when firing off a signal.
	
	However, I do find it interesting that sometimes, instead of there being latency, it would
instead fire the signal early, resulting in the jitter being a negative number. To me, it doesn't
make sense that the CPU would ever do something before it was even called in the program. Perhaps
this "negative" jitter is a result of how my timestamps are being recorded. Instead of taking a timestamp
at the precise moment that the signal is fired, I am just taking a timestamp after it completes the
wait_period function, and then again after the next completion, and finding the jitter between those
two moments. Because of this imprecision, it is likely that a portion of the jitter is coming from outside
my sig_wait call.

*/
