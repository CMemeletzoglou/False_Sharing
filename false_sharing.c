#define _GNU_SOURCE

#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


#if defined(__clang__)
        const char *compiler = "Clang";
#elif defined(__ICC) || defined(__INTEL_COMPILER)
        const char *compiler = "Intel ICC";
#elif defined(__GNUC__) || defined(__GNUG__)
        const char *compiler = "GNU GCC/G++";
#endif


#define NUM_PHYCORES num_phys_cores
#define NUM_VCORES 2 * NUM_PHYCORES


static struct timespec tstart, tend;
static cpu_set_t cpuset;
static FILE *fptr = NULL;
static int num_phys_cores = 0;
static int num_virt_cores = 0;
static int num_hardware_threads = 0;
static int smt_enabled;
static unsigned short rand_siblings = 0;
static unsigned short mode = 0;
static int *t_siblings = NULL;

int get_num_phys_cores()
{
        fptr = fopen("/sys/devices/system/cpu/smt/active", "r");
        int phys_cores = 0;
        int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
        
        if(fptr)
        {
                fscanf(fptr, "%i", &smt_enabled);

                if(smt_enabled == 1)
                        phys_cores = num_cores / 2;     //all desktop CPUs have at most two threads per core
                else
                        phys_cores = num_cores;

                fclose(fptr);
        }
        else
        {
                perror("Error trying to read /sys/devices/system/cpu/smt/active\n");
                exit(-1);
        }
        return phys_cores;
}


int *get_thread_siblings(int nphys_cores)
{
        int *thread_siblings = malloc(num_phys_cores * sizeof(int));
        
        char buf[1024];
        char *str = NULL;
        char *temp = NULL;
        unsigned int strlength;
        unsigned int size = 1;
        unsigned short index = 0;
        
        fptr = popen("grep -H . /sys/devices/system/cpu/cpu*/topology/thread_siblings_list | sort -n -t ':' -k 2 -u | grep -Eo '[0-9],[0-9]+(,[0-9])*'", "r");

        if(fptr)
        {
                while(fgets(buf, sizeof(buf), fptr) != NULL)
                {
                        strlength = strlen(buf);
                        temp = realloc(str, size + strlength);
                        if(temp == NULL)
                                perror("memory allocation error\n");
                        else
                        {
                                str = temp;
                        }
                        strcpy(str+size-1, buf);
                        size += strlength;
                }

                pclose(fptr);


                for(int i=0; i<NUM_PHYCORES; i++)
                {
                        sscanf(str, "%*d %*c %d", &thread_siblings[index]);
                        while(*str != '\n')
                                str++;
                        if(*str == '\n')
                                str++;

                        index++;
                }
        }
        else
        {
                perror("Error trying to read /sys/devices/system/cpu/cpu*/topology/thread_siblings_list\n");
                exit(-1);
        }

        return thread_siblings;
}

int get_rand_physical_cpu()
{
        return ( (int) ( (double)rand() / ( (double)RAND_MAX + 1) * NUM_PHYCORES ) );
}


double compute_wtime()
{
        double time = (tend.tv_sec - tstart.tv_sec) * 1000;     // time in msecs
        time += (tend.tv_nsec - tstart.tv_nsec) * 1e-6;         // add nsecs
        return time * 1e-3;                                     // convert msecs to seconds
}


void *work(void *arg)
{
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

        volatile double *ptr = (double *)arg;

        for(unsigned long i=0; i<1000000000; i++)
                *(ptr) += 1.0;

        return NULL;
}

unsigned short *parse_args(int argc, char **argv)
{
        // mode, 0 for false sharing , 1  for no-false sharing

        if(argc == 1)
        {
                rand_siblings = 1;
                mode = 0;
                return NULL;
        }

        unsigned short *cpu_ptr = calloc(2, sizeof(unsigned short));

        for(unsigned short i=1; i<argc; i++)
        {
                if(strcmp(argv[i], "--nofalse-sharing") == 0)
                        mode = 1;
                
                else if(strcmp(argv[i], "--rand-sibling-threads") == 0)
                        rand_siblings = 1;

                else if(strcmp(argv[i], "--cpu-list") == 0)
                {
                        i++;
                        int cpuA, cpuB;
                        sscanf(argv[i], "%d %*c %d", &cpuA, &cpuB);
                        cpu_ptr[0] = cpuA;
                        cpu_ptr[1] = cpuB;
                }

                else
                {
                        printf("Usage: %s [OPTIONS]\n\n"
                        "[OPTIONS]:\n"
                        "--nofalse-sharing :\tTo run without False Sharing\n"
                        "--rand-sibling-threads :\tTo run with a random pair of sibling threads\n", argv[0]);
                        exit(EXIT_FAILURE);
                }        
        }
        return cpu_ptr;
}


void init(int argc, char **argv)
{
        srand(time(NULL));
        CPU_ZERO(&cpuset);
        unsigned short *cpu_ptr = parse_args(argc, argv);
        num_phys_cores = get_num_phys_cores();
        
        if(cpu_ptr != NULL && rand_siblings == 0) //user specified cores
        {
                CPU_SET(cpu_ptr[0], &cpuset);
                CPU_SET(cpu_ptr[1], &cpuset);
        }

        /* either random sibling-threads or mode=1 (nofalse sharing) or default mode which is
         * false sharing with random sibling-threads */

        if(rand_siblings == 1 || mode == 1 || cpu_ptr == NULL)     
        {
                //thread i 's sibling is thread t_siblings[i]
                t_siblings = get_thread_siblings(num_phys_cores);

                int rand_cpu = get_rand_physical_cpu();
                int sibling_cpu = t_siblings[rand_cpu];
                printf("Running with random sibling pair: threads %d and %d\n", rand_cpu, sibling_cpu);
        
                CPU_SET(rand_cpu, &cpuset);
                CPU_SET(sibling_cpu, &cpuset);
        }
}


int main(int argc, char** argv)
{
        init(argc, argv);        

        printf("%d Physical Cores detected\n2 Hardware Threads per physical core\n",num_phys_cores);
        if(smt_enabled)
                printf("Simultaneous MultiThreading/Hyperthreading enabled\n");
        else
                printf("SMT/Hyperthreading is either not supported or disabled\n");

        if(mode == 0)
                printf("Running with False Sharing\n");
        else
                printf("Running without False Sharing\n");
      

        pthread_t tid;

        __attribute__((aligned(64))) double array[16] = {0};

        clock_gettime(CLOCK_REALTIME, &tstart);
    
        // if mode = 1 (i.e no False Sharing), one thread will  hammer array[8] , otherwise array[0]
        pthread_create(&tid, NULL, work, (void*)(&array[mode * 8]));
        work((void*)(&array[1]));

        pthread_join(tid, NULL);

        clock_gettime(CLOCK_REALTIME, &tend);
        printf("--------------------------------------------------------\n"
                "\nCompiler: %s\nTotal wtime elapsed = %f\tseconds\n"
                "--------------------------------------------------------\n", compiler, compute_wtime());
        
        
        free(t_siblings);
        return 0;
}