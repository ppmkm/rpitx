#include <unistd.h>
#include "librpitx/src/librpitx.h"
#include "stdio.h"
#include <cstring>
#include <signal.h>
#include <stdlib.h>
#include <sys/select.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

bool running=true;

#define PROGRAM_VERSION "0.1"
#define CTRL_SOCKET  "/var/run/sendiq"

static pthread_t control_thread;
static bool terminating = false;
static int udpPort = -1;

void SimpleTestFileIQ(uint64_t Freq)
{
	
}

void print_usage(void)
{

fprintf(stderr,\
"\nsendiq -%s\n\
Usage:\nsendiq [-i File Input][-s Samplerate][-l] [-f Frequency] [-h Harmonic number] \n\
-i            path to File Input \n\
-s            SampleRate 10000-250000 \n\
-f float      central frequency Hz(50 kHz to 1500 MHz),\n\
-l            loop mode for file input\n\
-h            Use harmonic number n\n\
-p uint       port to use for UDP control\n\
-t            IQ type (i16 default) {i16,u8,float,double}\n\
-?            help (this help).\n\
\n",\
PROGRAM_VERSION);

} /* end function print_usage */

static void
terminate(int num)
{
    running=false;
    terminating = true;
	fprintf(stderr,"Caught signal - Terminating %x\n",num);
   
}




#define SBUFSIZE 1500

static char sockbuf[SBUFSIZE];
static float SampleRate=48000;
static float SetFrequency=434e6;
static float NewSetFrequency=SetFrequency;


void *ctrl_thread_function(void * arg)
{
	int sockfd = socket(PF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
	  perror("ERROR opening socket");
//	struct sockaddr_un server;
	struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(udpPort);
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sockfd, (struct sockaddr *) &server,
                 sizeof(struct sockaddr_in)) == -1)
    {
             perror("bind");
             terminating = true;
             running = false;
             return NULL;
    }
	printf("bound ctrl socket\n");
	while (!terminating){
		ssize_t received = recv(sockfd,sockbuf,SBUFSIZE-1,0);
		sockbuf[received] = '\0';
		if (!strncmp("stop",sockbuf,4)){
		  terminating = true;
		  running = false;
		}
		if (!strncmp("F ",sockbuf,2) && received > 5){
			errno = 0;
			double freq = strtod(sockbuf+2,NULL);
			if (!errno) {
				NewSetFrequency = freq;
				printf("tuning from %f to %f\n", SetFrequency, NewSetFrequency);
			} else {
				printf("error processing: %s\n", sockbuf);
			}

		}

	}
	close(sockfd);
	printf("ctrl thread ends\n");
    return NULL;
}



#define MAX_SAMPLERATE 200000



int main(int argc, char* argv[])
{
	int a;
	int anyargs = 1;

	bool loop_mode_flag=false;
	char* FileName=NULL;
	int Harmonic=1;
	enum {typeiq_i16,typeiq_u8,typeiq_float,typeiq_double};
	int InputType=typeiq_i16;
	int Decimation=1;
	dbg_setlevel(1);
	while(1)
	{
		a = getopt(argc, argv, "i:f:s:h:p:lt:");
	
		if(a == -1) 
		{
			if(anyargs) break;
			else a='h'; //print usage and exit
		}
		anyargs = 1;	

		switch(a)
		{
		case 'i': // File name
			FileName = optarg;
			break;
		case 'f': // Frequency
			SetFrequency = atof(optarg);
			NewSetFrequency = SetFrequency;
			break;
		case 's': // SampleRate (Only needeed in IQ mode)
			SampleRate = atoi(optarg);
			if(SampleRate>MAX_SAMPLERATE) 
			{
				
				for(int i=2;i<12;i++) //Max 10 times samplerate
				{
					if(SampleRate/i<MAX_SAMPLERATE) 
					{
						SampleRate=SampleRate/i;
						Decimation=i;
						break;
					}	
				}
				if(Decimation==1)
				{	
					 fprintf(stderr,"SampleRate too high : >%d sample/s",10*MAX_SAMPLERATE);
					 exit(1);
				}	 
				else
				{	
					fprintf(stderr,"Warning samplerate too high, decimation by %d will be performed",Decimation);	 
				}		
			};	
			break;
		case 'h': // help
			Harmonic=atoi(optarg);
			break;
		case 'p': // port
			udpPort=atoi(optarg);
			break;
		case 'l': // loop mode
			loop_mode_flag = true;
			break;
		case 't': // inout type
			if(strcmp(optarg,"i16")==0) InputType=typeiq_i16;
			if(strcmp(optarg,"u8")==0) InputType=typeiq_u8;
			if(strcmp(optarg,"float")==0) InputType=typeiq_float;
			if(strcmp(optarg,"double")==0) InputType=typeiq_double;
			break;
		case -1:
        	break;
		case '?':
			if (isprint(optopt) )
 			{
 				fprintf(stderr, "sendiq: unknown option `-%c'.\n", optopt);
 			}
			else
			{
				fprintf(stderr, "sendiq: unknown option character `\\x%x'.\n", optopt);
			}
			print_usage();

			exit(1);
			break;			
		default:
			print_usage();
			exit(1);
			break;
		}/* end switch a */
	}/* end while getopt() */

	if(FileName==NULL) {fprintf(stderr,"Need an input\n");exit(1);}
	
	 for (int i = 0; i < 64; i++) {
        struct sigaction sa;

        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = terminate;
        sigaction(i, &sa, NULL);
    }

	FILE *iqfile=NULL;
	if(strcmp(FileName,"-")==0)
		iqfile=fopen("/dev/stdin","rb");
	else	
		iqfile=fopen(FileName	,"rb");
	if (iqfile==NULL) 
	{	
		printf("input file issue\n");
		exit(0);
	}

    #define IQBURST 4000

	int iqfd = fileno(iqfile);

	long usTimeout = (1000000L/SampleRate)*IQBURST*2; // us to fill IQBURST*2 at Samplerate

	struct timeval timeout;
	fd_set rfds;


	

	int SR=48000;
	int FifoSize=IQBURST*4;
	iqdmasync iqtest(SetFrequency,SampleRate,14,FifoSize,MODE_IQ);
	iqtest.SetPLLMasterLoop(3,4,0);
	iqtest.clkgpio::SetppmFromNTP();
//	iqtest.print_clock_tree();
	//iqtest.SetPLLMasterLoop(5,6,0);
	
	std::complex<float> CIQBuffer[IQBURST];	

	int selectDone = 0;
	printf ("starting TX with select timeout us %ld: \n", usTimeout);
	unsigned underrunCount = 0;
	unsigned selectcount = 0L;

	if (udpPort > 0 ) {
		if (pthread_create( &control_thread, NULL, ctrl_thread_function, (void*) NULL)){
			perror ("error starting control thread");
		} else
			printf ("started control thread on port: %d\n", udpPort);
	}

	while(running)
	{
		
			timeout.tv_sec = 0L;
			timeout.tv_usec = usTimeout;
	        if (NewSetFrequency != SetFrequency){
				printf("really tuning to %llu\n", (uint64_t)SetFrequency);
				SetFrequency = NewSetFrequency;
				iqtest.stop();
				printf("stopped dma\n");
				iqtest.clkgpio::disableclk(4);
				iqtest.setFrequency(SetFrequency);
//				iqtest.clkgpio::print_clock_tree();
				iqtest.clkgpio::SetppmFromNTP();
				printf("done really tuning to %llu\n", (uint64_t)SetFrequency);
	        }
			FD_ZERO(&rfds);
			FD_SET(iqfd,&rfds);
			int CplxSampleNumber=0;
			switch(InputType)
			{
				case typeiq_i16:
				{
					static short IQBuffer[IQBURST*2];
					int selval = select(iqfd+1,&rfds,NULL,NULL,&timeout);
					if (!selectDone)
					{
						printf("select Done %d\n", selval);
						selectDone = 1;
					}
					else
					{
						printf("S");
						fflush(stdout);
					}
					if (0 < selval)
					{
						int nbread=fread(IQBuffer,sizeof(short),IQBURST*2,iqfile);
						//if(nbread==0) continue;
						if(nbread>0)
						{
//						printf("R_%d\n",IQBuffer[0]);
						fflush(stdout);

							for(int i=0;i<nbread/2;i++)
							{
								if(i%Decimation==0)
								{
									CIQBuffer[CplxSampleNumber++]=std::complex<float>(IQBuffer[i*2]/32768.0,IQBuffer[i*2+1]/32768.0);
								}
							}
						}
						else
						{
							printf("End of file\n");
//							if(loop_mode_flag)
//							fseek ( iqfile , 0 , SEEK_SET );
//							else
								running=false;
						}
					}
					else if ((0 == selval) && feof(iqfile))
					{
						printf("End of file\n");
						if(loop_mode_flag)
						fseek ( iqfile , 0 , SEEK_SET );
						else
							running=false;
					}
					else if (0 == selval)
					{
//						if ((underrunCount++%10) == 0)
//						{
						       	printf("U");
							fflush(stdout);
//						}
//						continue;
					}
					else
					{
						perror("Error in select: ");
						running = false;
					}
					
				}
				break;
				case typeiq_u8:
				{
					static unsigned char IQBuffer[IQBURST*2];
					int nbread=fread(IQBuffer,sizeof(unsigned char),IQBURST*2,iqfile);
					
					if(nbread>0)
					{
						for(int i=0;i<nbread/2;i++)
						{
							if(i%Decimation==0)
							{	
								CIQBuffer[CplxSampleNumber++]=std::complex<float>((IQBuffer[i*2]-127.5)/128.0,(IQBuffer[i*2+1]-127.5)/128.0);
										
							}		 
							//printf("%f %f\n",(IQBuffer[i*2]-127.5)/128.0,(IQBuffer[i*2+1]-127.5)/128.0);
						}
					}
					else 
					{
						printf("End of file\n");
						if(loop_mode_flag)
						fseek ( iqfile , 0 , SEEK_SET );
						else
							running=false;
					}
				}
				break;
				case typeiq_float:
				{
					static float IQBuffer[IQBURST*2];
					int selval = select(iqfd+1,&rfds,NULL,NULL,&timeout);
					if (!selectDone)
					{
						printf("select Done %d\n", selval);
						selectDone = 1;
					}
					else
					{
				//		printf("S");
				//		fflush(stdout);
					}
					if (0 < selval)
					{
					
						int nbread=fread(IQBuffer,sizeof(float),IQBURST*2,iqfile);
						//if(nbread==0) continue;
						if(nbread>0)
						{
							for(int i=0;i<nbread/2;i++)
							{
								if(i%Decimation==0)
								{	
									CIQBuffer[CplxSampleNumber++]=std::complex<float>(IQBuffer[i*2],IQBuffer[i*2+1]);
											
								}		 
								//printf("%f %f\n",(IQBuffer[i*2]-127.5)/128.0,(IQBuffer[i*2+1]-127.5)/128.0);
							}
						}
						else 
						{
							printf("End of file\n");
							if(loop_mode_flag)
							fseek ( iqfile , 0 , SEEK_SET );
							else
								running=false;
						}
					}
					else if ((0 == selval) && feof(iqfile))
					{
						printf("End of file\n");
						if(loop_mode_flag)
						fseek ( iqfile , 0 , SEEK_SET );
						else
							running=false;
					}
					else if (0 == selval)
					{
						if ((underrunCount++%10) == 0) 
						{
						       	printf("U");
							fflush(stdout);
						}
						continue;
					}
					else
					{
						perror("Error in select: ");
						running = false;
					}
				}
				break;	
				case typeiq_double:
				{
					static double IQBuffer[IQBURST*2];
					int nbread=fread(IQBuffer,sizeof(double),IQBURST*2,iqfile);
					//if(nbread==0) continue;
					if(nbread>0)
					{
						for(int i=0;i<nbread/2;i++)
						{
							if(i%Decimation==0)
							{	
								CIQBuffer[CplxSampleNumber++]=std::complex<float>(IQBuffer[i*2],IQBuffer[i*2+1]);
										
							}		 
							//printf("%f %f\n",(IQBuffer[i*2]-127.5)/128.0,(IQBuffer[i*2+1]-127.5)/128.0);
						}
					}
					else 
					{
						printf("End of file\n");
						if(loop_mode_flag)
						fseek ( iqfile , 0 , SEEK_SET );
						else
							running=false;
					}
				}
				break;	
			
		}

		iqtest.SetIQSamples(CIQBuffer,CplxSampleNumber,Harmonic);
	}
	printf("stopping iqtest\n");
	iqtest.stop();
	unlink(CTRL_SOCKET);
	fclose(iqfile);
	if (pthread_cancel(control_thread)){
		perror ("error stopping control thread");
	} else
		printf ("canceled control thread\n");
	printf("main ends\n");
}


