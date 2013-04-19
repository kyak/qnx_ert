/* $Revision: 1.31.4.16 $
 * Copyright 1994-2008 The MathWorks, Inc.EX
 *
 * File: ert_main.c
 *
 * Abstract:
 *
 *   An embedded real-time main that runs the generated Real-Time
 *   Workshop code under most operating systems.  Based on the value
 *   of NUMST and definition of MULTITAKING a single rate/single task, 
 *   multirate/single task or multirate/multitask step function is 
 *   employed.
 *
 *   This file is a useful starting point when targeting a custom
 *   floating point processor or integer microcontroller.
 *
 *   Alternatively, you can generate a sample ert_main.c file with the
 *   generated code by selecting the "Generate a sample main program"
 *   option.  In this case, ert_main.c is precisely customized to the
 *   model requirements.  Demo models for deploying the generated code
 *   on a bare board target (i.e., no operating system) for the various
 *   scenarios are:
 *
 *     ecsample1 (single rate)
 *     ecsample2 (multirate/singletasking)
 *     ecsample3 (multirate/multitaksing)
 *
 *   Demo models for deploying the generated code on a real-time
 *   operating system for the various scenarios are:
 *
 *     ecsample3 (single rate)
 *     ecsample4 (multirate/singletasking)
 *     ecsample5 (multirate/multitaksing)
 *
 * Required Defines:
 *
 *   MODEL - Model name
 *   NUMST - Number of sample times
 *
 */

/*==================*
 * Required defines *
 *==================*/

#ifndef MODEL
# error Must specify a model name.  Define MODEL=name.
#else
/* create generic marcros that work with any model */
# define EXPAND_CONCAT(name1,name2) name1 ## name2
# define CONCAT(name1,name2) EXPAND_CONCAT(name1,name2)
# define MODEL_INITIALIZE CONCAT(MODEL,_initialize)
# define MODEL_STEP       CONCAT(MODEL,_step)
# define MODEL_TERMINATE  CONCAT(MODEL,_terminate)
# define RT_MDL           CONCAT(MODEL,_M)
#endif

#ifndef NUMST
# error Must specify the number of sample times.  Define NUMST=number.
#endif

#if ONESTEPFCN==0
#error Separate output and update functions are not supported by ert_main.c. \
You must update ert_main.c to suit your application needs, or select \
the 'Single output/update function' option.
#endif



#if MULTI_INSTANCE_CODE==1
#error The static version of ert_main.c does not support reusable \
code generation.  Either deselect ERT option 'Generate reusable code', \
select option 'Generate an example main program', or modify ert_main.c for \
your application needs.
#endif



/*==========*
 * Includes *
 *==========*/

#include "simstruc_types.h"
#include "rtwtypes.h"
#if !defined(INTEGER_CODE) || INTEGER_CODE == 0
# include <stdio.h>    /* optional for printf */
#else
#ifdef __cplusplus
extern "C" {
#endif
  extern int printf(const char *, ...); 
  extern int fflush(void *);
#ifdef __cplusplus
}
#endif
#endif

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/procfs.h>
#include <sys/states.h>
#include <sys/types.h>
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <sys/syspage.h>
#include <sys/socket.h>
#include <sys/siginfo.h>
#include <rpc/rpc.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <process.h>
#include <string.h>
#include <math.h>
#include <unistd.h>




#include <string.h>    /* optional for strcmp */
#include "autobuild.h" /* optional for automated builds */

#ifdef MODEL_STEP_FCN_CONTROL_USED
#error The static version of ert_main.c does not support model step function prototype control.
#endif

/* moved from simstruc_types.h as only ert_main.c needs it */
/*========================* 
 * Setup for multitasking * 
 *========================*/

/* 
 * Let MT be synonym for MULTITASKING (to shorten command line for DOS) 
 */
#if defined(MT)
# if MT == 0
# undef MT
# else
# define MULTITASKING 1
# endif
#endif

#if defined(TID01EQ) && TID01EQ == 1 && NCSTATES==0
#define DISC_NUMST (NUMST - 1)
#else 
#define DISC_NUMST NUMST
#endif

#if defined(TID01EQ) && TID01EQ == 1
#define FIRST_TID 1
#else 
#define FIRST_TID 0
#endif

/*====================*
 * External functions *
 *====================*/

#if defined(INCLUDE_FIRST_TIME_ARG) && INCLUDE_FIRST_TIME_ARG==0
extern void MODEL_INITIALIZE(void);
#else
extern void MODEL_INITIALIZE(boolean_T firstTime);
#endif
extern void MODEL_TERMINATE(void);

#if DISC_NUMST == 1
  extern void MODEL_STEP(void);       /* single rate step function */
#else
  extern void MODEL_STEP(int_T tid);  /* multirate step function */
#endif


/*==================================*
 * Global data local to this module *
 *==================================*/
#ifndef MULTITASKING
static boolean_T OverrunFlags[1];    /* ISR overrun flags */
static boolean_T eventFlags[1];      /* necessary for overlapping preemption */
#else
static boolean_T OverrunFlags[NUMST];
static boolean_T eventFlags[NUMST]; 
#endif

typedef struct threadStructTag {
   char*           name;
   int             channelId;
   int             sampleId;
   pthread_t       threadId;
   int             priority;
   int             thExecCount;
   boolean_T       schedThread ;
   int             stackSize;
}threadStructT;
    
#define MAX_NUM_THREADS  64

#define  GBL_CMD_START_THREAD  1000

#define  GBL_START_PRIORITY  20

#define  STACK_SIZE  65536

//static threadStructT GBLthreadArr[MAX_NUM_THREADS];
static threadStructT  *GBLthreadArr;
/*===================*
 * Visible functions *
 *===================*/

/* Function: tBaseRate =========================================================
 * Abstract:
 *  This is the entry point for the base-rate task.  This task executes
 *  the fastest rate blocks in the model each time its semaphore is given
 *  and gives semaphores to the sub tasks if they need to execute.
 */

#define MY_PULSE_CODE   _PULSE_CODE_MINAVAIL
typedef union {
        struct _pulse   pulse;
        /* your other message structures would go 
           here too */
} my_message_t;



static void *tBaseRate(threadStructT  *thPtr)
{
   int_T                   i;
   real_T                  tnext;
   int_T                   finalstep = 0;
   struct                  sigevent         event;
   struct                  itimerspec       itime;
   timer_t                 timer_id;
   my_message_t            msg;
   int                     chid;
   int                     rcvid;
   real_T                  minorStep;
   real_T                  lval_nsec;


   printf("Starting tBaseRate\n");
   
   minorStep = ((RT_MDL)->Timing.stepSize0)*1000000;
   //minorStep = 1.0*1000000;
   printf ("minorStep is %f\n", minorStep);
 
   
   /*
    * Initialize the thExecCount and schedThread.
    */
   thPtr->thExecCount = 0;
   thPtr->schedThread = FALSE;
   
   event.sigev_notify = SIGEV_PULSE;
   event.sigev_coid = ConnectAttach(ND_LOCAL_NODE, 0, 
                                   thPtr->channelId, 
                                    _NTO_SIDE_CHANNEL, 0);
   event.sigev_priority = getprio(0);
   event.sigev_code = MY_PULSE_CODE;
   
   timer_create(CLOCK_REALTIME, &event, &timer_id);

   itime.it_value.tv_sec = (int_T)(minorStep/1000000);
   lval_nsec = fmod((real_T)minorStep,((real_T)1000000.0)) * 1000.0;
   itime.it_value.tv_nsec = (long long)lval_nsec;
   itime.it_interval.tv_sec = (int_T)(minorStep/1000000);

   itime.it_interval.tv_nsec = (long long)lval_nsec;
   timer_settime(timer_id, 0, &itime, NULL);
 
  
   printf ("minorStep %f\n", minorStep);
   printf ("tv_sec is %d\n",  itime.it_interval.tv_sec);
   printf ("tv_nsec is %d\n", itime.it_interval.tv_nsec);   
   printf ("Created timer \n");
   
  
   while(1) {
       
       printf("timerChannelId is %d\n", thPtr->channelId);
       rcvid = MsgReceive(thPtr->channelId, &msg, sizeof(msg), NULL);
      
       if (rcvid == 0) { /* we got a pulse */
         
        
            /***********************************************
             * Check and see if base step time is too fast *
             ***********************************************/
            if (GBLthreadArr[0].thExecCount++) {
                rtmSetErrorStatus(RT_MDL, "Overrun");
            }

            /*************************************************
             * Check and see if an error status has been set *
             * by an overrun or by the generated code.       *
             *************************************************/
            if (rtmGetErrorStatus(RT_MDL) != NULL) {
                return;
            }      

            /*************************************************
             * Update EventFalgs and check subrate overrun   *
             *************************************************/
#ifdef MULTITASKING
            for (i = FIRST_TID+1; i < NUMST; i++) {
               if (rtmStepTask(RT_MDL,i)) {
                  if (GBLthreadArr[i].thExecCount > 1) {
                     /* Sampling too fast */
                     rtmSetErrorStatus(RT_MDL, "Overrun");
                     GBLthreadArr[i].schedThread = FALSE;
                  }
                  else {
                      GBLthreadArr[i].schedThread = TRUE;
                  }
               }
               
               if (++rtmTaskCounter(RT_MDL,i) == rtmCounterLimit(RT_MDL,i))
                    rtmTaskCounter(RT_MDL, i) = 0;
            }
#endif
            /* Set model inputs associated with base rate here */

            /*******************************************
             * Step the model for the base sample time *
             *******************************************/
            printf("Stepping Model rate 0\n");
            MODEL_STEP(0);

       
            
            /* Get model outputs associated with base rate here */

            /************************************************************************
             * Model step complete for base sample time, now it is okay to          *
             * re-interrupt this ISR.                                               *
             ************************************************************************/

            
            thPtr->thExecCount--;
            thPtr->schedThread = FALSE;

            //#ifdef EXT_MODE
              // rtExtModeUploadCheckTrigger(NUMST);
              // rtExtModeUpload(FIRST_TID,(RT_MDL)->Timing.t[FIRST_TID]);
            //#endif
            
            //#ifdef EXT_MODE
            //   rtExtModeCheckEndTrigger();
            //#endif
            
            /*********************************************************
             * Step the model for any other sample times (sub-rates) *
             *********************************************************/
#ifdef MULTITASKING
            for (i = FIRST_TID+1; i < NUMST; i++) {

                if (GBLthreadArr[i].schedThread) {

                    /* Set model inputs associated with subrate here */

                    /******************************************
                     * Step the model for for sample time "i" *
                     ******************************************/

                    /* Signal the channel for the task */
                     printf("sending pulse to %d\n", i);
                     MsgSendPulse (GBLthreadArr[i].channelId,
                                   GBLthreadArr[i].priority,
                                   GBL_CMD_START_THREAD,
                                   GBL_CMD_START_THREAD);

                 }
            }
#endif
        }
      
     }
   
}  /* tBaseRate */  


/* Function: tSubRate ==========================================================
 * Abstract:
 *  This is the entry point for each sub-rate task.  This task simply executes
 *  the appropriate  blocks in the model each time the passed semaphore is
 *  given.  This routine never returns.
 */

static void *tSubRate(threadStructT *thPtr)
{
   struct _pulse           waitPulse;  
   int                     waitChannelId;
   int                     threadId;
   int                     rcvStat;
   
   
   thPtr->thExecCount = 0;
   thPtr->schedThread = FALSE;
    printf("Starting tSubRate\n");
    while(1) {
     
       rcvStat = MsgReceive(thPtr->channelId,
                            &waitPulse,
                            sizeof(waitPulse), NULL);

       if (rcvStat < 0) {
         perror("Error receiving pulse\n");
         break;
       }
         
       MODEL_STEP(thPtr->threadId);
       printf("Stepped subrate %d\n", thPtr->threadId);
       #if 0
       //rtExtModeUpload(thPtr->sampleId, (RT_MDL)->Timing.t[thPtr->sampleId]);
       #endif
       thPtr->thExecCount--;
       thPtr->schedThread = FALSE;
    }
   
    return(NULL);
    
} /* end tSubRate */


/* Function: tgtCreateThread ========================================================
 *
 * Abstract:
 *    Creates a thread for the target
 */



static pthread_t  tgtCreateThread(char *threadName,  void* (*threadRoot)(void* ),
                                   void *threadArg, int priority, int stackSize)
{
    int             prio_max;
    int             prio_min;
    struct _clockperiod clockperiodstruct;
    struct _clockperiod new_clockperiodstruct;
    int             clock_period_stat;
    int             policy;
    pthread_attr_t  attr;
    sched_param_t   schedParam;
    int             GBL_chid;
    int             pulse_id;
    unsigned int    GBL_cpu_freq;
    int             i;
    int             thIndex;
    int             sched_th_index;
    int             ct;
    int             status;
    double          dbl;
    pthread_t       threadId;

    
    /*
     * Get the default attributes of threads and modify them.
     */
  
     
     pthread_attr_init(&attr); 
     pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); 
     pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);   
     pthread_attr_getschedparam(&attr, &schedParam);
     
     /*
      * Create the user thread.
      */
     
     
      pthread_attr_setstacksize(&attr, stackSize);
      pthread_create(&threadId, &attr,
                     threadRoot, threadArg);
      
      /*
       * Set the schedulng parameters for the thread.
       */
      
      policy = SCHED_RR;
      schedParam.sched_priority = priority;
      pthread_setschedparam (threadId, policy, &schedParam);

      
      /*
       * Check the scheduling parameters
       */
       pthread_getschedparam(threadId, &policy,&schedParam);
      
       printf("In tgtCreateThread 4\n" );
       
      return threadId;     
}

/* Function: rt_InitModel ====================================================
 * 
 * Abstract: 
 *   Initialized the model and the overrun-flags
 *
 */
void rt_InitModel(void)
{
#if defined(MULTITASKING)
    int i;
    
    GBLthreadArr = (threadStructT *)malloc(sizeof(threadStructT)*(NUMST+1));
    for(i=0; i < NUMST; i++) {
        GBLthreadArr[i].stackSize = STACK_SIZE;
        GBLthreadArr[i].thExecCount = 0;
        GBLthreadArr[i].schedThread = FALSE;
        GBLthreadArr[i].sampleId = i;
    }
#else
    GBLthreadArr = (threadStructT *)malloc(sizeof(threadStructT));
    GBLthreadArr[0].stackSize = STACK_SIZE;
    GBLthreadArr[0].thExecCount = 0;
    GBLthreadArr[0].schedThread = FALSE;
#endif

    /************************
     * Initialize the model *
     ************************/
#if defined(INCLUDE_FIRST_TIME_ARG) && INCLUDE_FIRST_TIME_ARG==0
    printf("MODEL_INITIALIZE()\n");
    MODEL_INITIALIZE();
#else
    printf(" MODEL_INITIALIZE(1)\n");
    MODEL_INITIALIZE(1);
#endif
}


int_T rt_TermModel(void)
{
   // MODEL_TERMINATE();
    
    {
//         const char_T *errStatus = (const char_T *) (rtmGetErrorStatus(RT_MDL));
//         int_T i;
//         
//         if (errStatus != NULL && strcmp(errStatus, "Simulation finished")) {
//             (void)printf("%s\n", rtmGetErrorStatus(RT_MDL));
//             for (i = 0; i < NUMST; i++) {
//                 if (OverrunFlags[i]) {
//                     (void)printf("ISR overrun - sampling rate too"
//                                  "fast for sample time index %d.\n", i);
//                 }
//             }
//             return(1);
//         }
    }
    
    return(0);
}




/* Function: main =============================================================
 *
 * Abstract:
 *   Execute model on a generic target such as a workstation.
 */
int_T main(int_T argc, const char_T *argv[])
{
    UNUSED_PARAMETER(argc);
    UNUSED_PARAMETER(argv);
    int                    i;
    int                    priority;
    int                    prio_max;
    int                    dbg=0;
    
  
    printf ("self priority is %d\n",  getprio(0));

    /*******************************************
     * warn if the model will run indefinitely *
     *******************************************/
#if MAT_FILE==0 && !defined(NO_MAT_FILE)
    printf("warning: the simulation will run with no stop time; "
           "to change this behavior select the 'MAT-file logging' option\n");
    fflush(NULL);
#endif


    rt_InitModel();
    
    printf ("In main\n");
    
    /************************
     * initialize the model *
     ************************/
    prio_max = sched_get_priority_max(SCHED_RR);
    priority = prio_max;
    priority = getprio(0) + 5;
   
    for (i = FIRST_TID + 1; i < NUMST; i++) {
        static char taskName[20];
        
        sprintf(taskName, "tRate%d", i);
        GBLthreadArr[i].channelId =  ChannelCreate(_NTO_CHF_FIXED_PRIORITY);
      
        
        GBLthreadArr[i].stackSize = STACK_SIZE;
         
       
        GBLthreadArr[i].threadId = tgtCreateThread (taskName, tSubRate, &GBLthreadArr[i], 
                priority-i, GBLthreadArr[i].stackSize);  
      
    }
    priority = getprio(0);
    printf("prioriy is %d\n", priority);  
    GBLthreadArr[0].channelId =  ChannelCreate(_NTO_CHF_FIXED_PRIORITY);
    GBLthreadArr[0].stackSize = STACK_SIZE;
    GBLthreadArr[0].threadId = tgtCreateThread ("tBaseRate", tBaseRate, &GBLthreadArr[0], 
                priority+1, GBLthreadArr[0].stackSize);
    #if 0
       GBLthreadArr[0].threadId = tgtCreateThread("tBaseRate", tBaseRate, &GBLthreadArr[0], 
                priority, GBLthreadArr[0].stackSize);   
    #endif
    
     printf("Created based thread\n");
     for(;;);
    
    /***********************************************************************
     * Execute (step) the model.  You may also attach rtOneStep to an ISR, *
     * in which case replace the call to rtOneStep with a call to a        *
     * background task.  Note that the generated code sets error status    *
     * to "Simulation finished" when MatFileLogging is specified in TLC.   *
     ***********************************************************************/
     
     for(;;) {};

    /*******************************
     * Cleanup and exit (optional) *
     *******************************/
    return;
}

/* [EOF] ert_main.c */
