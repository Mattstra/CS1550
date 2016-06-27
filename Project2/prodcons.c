#include <stdio.h>
#include <stdlib.h>
#include <linux/unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>


int num_prods = 0;
int num_cons = 0;
int buffer_size = 0;
int status;
void *sem_mem;
void *shared_buffer;

struct cs1550_sem
{
	int value;
	struct Node *front;
	struct Node *end;
};

//up and down syscall wrapper functions
void down(struct cs1550_sem *sem)
{
	syscall(__NR_cs1550_down, sem);
}

void up(struct cs1550_sem *sem)
{
	syscall(__NR_cs1550_up, sem);
}

int main(int argc, char *argv[])
{	
	if(argc < 4)
	{
		printf("Enter all required command line arguments \n");
		exit(1);
	}

	else
	{
		num_prods = atoi(argv[1]);
		num_cons = atoi(argv[2]);
		buffer_size = atoi(argv[3]);
	
		 /* shared memory region for multiple processes
		  * allocate memory for semaphores for multiple processes
		  * 3 semaphores: empty, full, mutex
		  */

		sem_mem = mmap(NULL, 3*sizeof(struct cs1550_sem), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);

		struct cs1550_sem *empty = (struct cs1550_sem*)sem_mem;
		struct cs1550_sem *full = (struct cs1550_sem*)sem_mem+1;
		struct cs1550_sem *mutex = (struct cs1550_sem*)sem_mem+2;

		empty->value = buffer_size;
		empty->end = NULL;
		empty->front = NULL;

		full->value = 0;		//initially buffer is not full so value = 0
		full->end = NULL;
		full->front = NULL;

		mutex->value = 1;
		mutex->end = NULL;
		mutex->front = NULL;
		
	
		//shared buffer allocation
		//need space for producer and consumer pointers
		//also need a pointer for n size of buffer
		shared_buffer = mmap(NULL, sizeof(int)*(buffer_size +3), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);

		int *prod_ptr;
		int *con_ptr;
		int *buff_ptr;
		int *size_ptr;
 
		size_ptr = (int *)shared_buffer;
		prod_ptr = (int *)shared_buffer +1;
		con_ptr = (int *)shared_buffer +2;
		buff_ptr = (int *)shared_buffer +3;

	
		*prod_ptr = 0;
		*con_ptr = 0;
		*size_ptr = buffer_size;

		int i = 0;
		int j = 0;
		int pitem;
		int citem;

		//create specified number of producer processes
		for(i = 0; i < num_prods; i++)
		{
			if(fork() == 0) 	//child process
			{
				while(1)
				{
					down(empty);
					down(mutex);

					//have entered critical section
					pitem = *prod_ptr;
					buff_ptr[*prod_ptr] = pitem;		//produce item in buffer
					printf("Producer %c Produced: %d\n", i+65, pitem);
					*prod_ptr = (*prod_ptr+1) % *size_ptr;

					up(mutex);
					up(full);
				}
			}
		}

		for(j = 0; j < num_cons; j++)
		{
			if(fork() == 0)
			{
				while(1)
				{
					down(full);
					down(mutex);

					//enter critical section
					citem = buff_ptr[*con_ptr];
					printf("Consumer %C consumed: %d\n", j+65, citem);
					*con_ptr = (*con_ptr+1) % *size_ptr;

					up(mutex);
					up(empty);
				}
			}
			
		}


	
		wait(&status);		
	}
	return 0;
}
