#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned int address;
char mode;
int num_frames = 0;
char algorithm[5];
int refresh = 0;
FILE *trace_file;
char filename[25];

#define page_size 2<<12
#define num_pages 2<<20


struct Node
{
	int position;
	struct Node* next;
};


struct Page
{
	int number;
	int frame;
	int referenced;
	int valid;
	int dirty;
	unsigned char counter;
	struct Page* next;	
};


/* array of linked lists to store positions of each page */
struct Node* page_array[num_pages];
struct Node* new_node[num_pages];
struct Node* pos_ptr[num_pages];
struct Node* temp[num_pages];
struct Page* page_ptr[num_pages];

void sim_opt(int frames)
{
	int pNum;
	int curr_line = 0;
	int curr_frame = 0;
	int page_faults = 0;
	int disk_writes = 0;
	int pos_array[num_frames];
	int mem_accesses = 0;

	/* arary holding # of frames */
        int frame_array[num_frames];
	int i;
	for(i = 0; i < num_frames; i++)
		frame_array[i] = -1;

	struct Page* page_table = malloc(sizeof(struct Page));	

        while(fscanf(trace_file, "%x %c", &address, &mode) != EOF)
        {
		/* extract page number from msb 20 bits
		 * use page number as index into array
		 * add position in trace file as a child node
		 */

		pNum = address >> 12;
				
		//if linked list is empty
		if(page_array[pNum]->next == NULL)
                {
                	page_array[pNum] = malloc(sizeof(struct Node));
			page_array[pNum]->position = curr_line;
			new_node[pNum] = malloc(sizeof(struct Node));
			new_node[pNum] = page_array[pNum];
			new_node[pNum]->next = malloc(sizeof(struct Node));
			new_node[pNum] = new_node[pNum]->next;
			page_array[pNum]->next = new_node[pNum];
		}
		
		//when list isn't empty
		else
		{	new_node[pNum]->position = curr_line;
			new_node[pNum]->next = malloc(sizeof(struct Node));
			new_node[pNum] = new_node[pNum]->next;
		}                        
                        		
		curr_line++;
	}
			

	curr_line = 0;	
	trace_file = fopen(filename, "r");
	struct Page* page_entry = malloc(sizeof(struct Page));
	
	page_entry = page_table;
	int k;
	for(k = 0; k < num_pages; k++)
	{
		page_entry->number = k;
		page_entry->valid = 0;
		page_entry->referenced = 0;
		page_entry->dirty = 0;
		page_entry->next = malloc(sizeof(struct Page));
		page_ptr[k] = page_entry;
		page_entry = page_entry->next;
	}

		


	while(fscanf(trace_file, "%x %c", &address, &mode) != EOF)
	{
		pNum = address >> 12;
		page_entry = page_ptr[pNum];		
		if(mode == 87)		//87 is equal to 'w'
		{
			page_entry->dirty = 1;
		}

		curr_line++;
		page_entry->referenced = 1;
		if(page_entry->valid == 1)
                        printf("%x hit\n", address);
		
		else
		{
			/* Handle page faults by swapping in pages */
			page_faults++;

			/* There are available frames */
			if(curr_frame < num_frames)
			{
				page_entry->frame = curr_frame;
				page_entry->valid = 1;
				frame_array[curr_frame] = page_entry->number;
				printf("%x Page fault - no eviction\n", address);

			}

			/* No available frames so have to evict */
			else
			{
				int i;
				int skip = 0;
				int curr_page;
				for(i = 0; i < num_frames; i++)
				{
					curr_page = frame_array[i];
					if(temp[curr_page] == NULL)
                                        {						
                                                pos_ptr[curr_page] = page_array[curr_page];
                                                temp[curr_page] = pos_ptr[curr_page];
                                        }
		
					else
                                        {
                                                pos_ptr[curr_page] = temp[curr_page];
                                        }

					new_node[curr_page] = page_array[curr_page];

					if((pos_ptr[curr_page]->position <= curr_line) && pos_ptr[curr_page]->next != NULL)
					{
						new_node[curr_page] = pos_ptr[curr_page];

						while((new_node[curr_page]->position <= curr_line) && new_node[curr_page]->next != NULL)
						{
							new_node[curr_page] = new_node[curr_page]->next;
						}

						pos_ptr[curr_page] = new_node[curr_page];
						temp[curr_page] = pos_ptr[curr_page];
					}		
											
					if(pos_ptr[curr_page]->position <= curr_line)
						pos_ptr[curr_page]->position = 1000000;
	
					pos_array[i] = pos_ptr[curr_page]->position;
				}
		
				int furthest;
				int tempf;
				int index;
				int j;
				for(j = 0; j < num_frames; j++)	
				{
					if(j==0)
					{
						//do nothing
					}
					else
					{
						
						if(pos_array[j-1] > pos_array[j])
						{
							tempf = pos_array[j-1];
							if(tempf > furthest)
							{
								furthest = tempf;
								index = j-1;
							}
						}
					}
				}
				

				/* evict page at j position in frame array */
				int evict_page = frame_array[index];
				page_entry = page_ptr[evict_page];

				page_entry->frame = -1;
				page_entry->valid = 0;
				page_entry->referenced = 0;

				if(page_entry->dirty == 1)
				{
					printf("%x Page fault - evict dirty\n", address);	
					disk_writes++;
				}
				else
				{
					printf("%x Page fault - evict clean\n", address);
				}
				/* swap in page */
				frame_array[index] = pNum;
				page_entry = page_table;
				page_entry = page_ptr[pNum];
				page_entry->valid = 1;
				page_entry->frame = index;
			}
		curr_frame++;
		}
		mem_accesses++;
	}
	printf("%s\n Number of frames: %d\n Total memory accesses: %d\n Total page faults: %d\n Total writes to disk: %d\n", algorithm, num_frames, mem_accesses, page_faults,disk_writes);	
}


void sim_clock(int frames)
{
	int curr_line = 0;
	int pNum;
	int evict_page;
	int page_faults = 0;
	int disk_writes = 0;
	int mem_accesses = 0;
	int curr_frame;
	int clock_pos = 0;
	curr_frame = 0;		
        trace_file = fopen(filename, "r");
	struct Page* page_table = malloc(sizeof(struct Page));
        struct Page* page_entry = malloc(sizeof(struct Page));

        page_entry = page_table;
        int k;
        for(k = 0; k < num_pages; k++)
        {
                page_entry->number = k;
                page_entry->valid = 0;
                page_entry->referenced = 1;
                page_entry->dirty = 0;
                page_entry->next = malloc(sizeof(struct Page));
                page_ptr[k] = page_entry;
                page_entry = page_entry->next;
        }

	/* arary holding # of frames */
        int frame_array[num_frames];
        int i;
        for(i = 0; i < num_frames; i++)
                frame_array[i] = -1;

	while(fscanf(trace_file, "%x %c", &address, &mode) != EOF)
        {
                pNum = address >> 12;
                page_entry = page_ptr[pNum];
                if(mode == 87)          //87 is equal to 'w'
                {
                        page_entry->dirty = 1;
                }

                curr_line++;
                if(page_entry->valid == 1)
                        printf("%x hit\n", address);
                else
                {
                        /* Handle page faults by swapping in pages */
                        page_faults++;

                        /* There are available frames */
                        if(curr_frame < num_frames)
                        {
                                page_entry->frame = curr_frame;
                                page_entry->valid = 1;
                                frame_array[curr_frame] = page_entry->number;
                                printf("%x Page fault - no eviction\n", address);

                        }

			/* No available frames so evict based on clock algorithm */
			else
			{
				while(1)
				{
					/* Search for page with a reference bit of 0 */
					int frame_page = frame_array[clock_pos];
					if(page_ptr[frame_page]->referenced == 1)
					{
						if(clock_pos = num_frames -1)
						{
							clock_pos = -1;
						}
						clock_pos++;
						page_ptr[frame_page]->referenced = 0;
					}

					/* evict page */
					else
					{
						evict_page = frame_page;
						break;
					}
				}
				 page_entry = page_ptr[evict_page];

                                page_entry->frame = -1;
                                page_entry->valid = 0;
                                page_entry->referenced = 0;

                                if(page_entry->dirty == 1)
                                {
                                        printf("%x Page fault - evict dirty\n", address);
                                        disk_writes++;
                                }
                                else
                                {
                                        printf("%x Page fault - evict clean\n", address);
                                }

				/* swap in page */
                                frame_array[clock_pos] = pNum;
                                page_entry = page_table;
                                page_entry = page_ptr[pNum];
                                page_entry->valid = 1;
                                page_entry->frame = clock_pos;
				page_entry->referenced = 1;								
						
			}
		}
	curr_frame++;
	mem_accesses++;
	}
	printf("%s\n Number of frames: %d\n Total memory accesses: %d\n Total page faults: %d\n Total writes to disk: %d\n", algorithm, num_frames, mem_accesses, page_faults, disk_writes);

}


void sim_nru(int frames)
{
	int curr_line = 0;
        int pNum;
        int evict_page;
        int page_faults = 0;
        int disk_writes = 0;
        int mem_accesses = 0;
        int curr_frame = 0;
        trace_file = fopen(filename, "r");
        struct Page* page_table = malloc(sizeof(struct Page));
        struct Page* page_entry = malloc(sizeof(struct Page));

        page_entry = page_table;
        int k;
        for(k = 0; k < num_pages; k++)
        {
                page_entry->number = k;
                page_entry->valid = 0;
                page_entry->referenced = 1;
                page_entry->dirty = 0;
                page_entry->next = malloc(sizeof(struct Page));
                page_ptr[k] = page_entry;
                page_entry = page_entry->next;
        }

	 /* arary holding # of frames */
        int frame_array[num_frames];
        int i;
        for(i = 0; i < num_frames; i++)
                frame_array[i] = -1;

	
	
	while(fscanf(trace_file, "%x %c", &address, &mode) != EOF)
        {
                pNum = address >> 12;
                page_entry = page_ptr[pNum];
                if(mode == 87)          //87 is equal to 'w'
                {
                        page_entry->dirty = 1;
                }
	
		/* clear reference bits every refresh period */
               	if(mem_accesses % refresh == 0)
                {
                         int i;
                  	for(i = 0; i < num_frames; i++)
			{
				int page = frame_array[i];
				if(page == -1)
				{
					break;
				}
				else
				{
					page_ptr[page]->referenced = 0;
				}
			}       
                } 

		curr_line++;
              
		 if(page_entry->valid == 1)
                        printf("%x hit\n", address);

                else
                {
                        /* Handle page faults by swapping in pages */
                        page_faults++;

			/* There are available frames */
                        if(curr_frame < num_frames)
                        {
				page_entry->frame = curr_frame;
                                page_entry->valid = 1;
                                frame_array[curr_frame] = page_entry->number;
                                printf("%x Page fault - no eviction\n", address);
				curr_frame++;

                        }
			
			else
			{
				
				/* No available frames so evict using NRU algorithm
				 * not referenced, not dirty
				 * not referenced, dirty
				 * referenced, not dirty
				 * referenced dirty
				 */

				int k;
				for(k = 0; k < num_frames; k++)
				{
					int page = frame_array[k];
					if((page_ptr[page]->dirty == 0)&& (page_ptr[page]->referenced ==0))
					{
						//evict page
						printf("%x Page fault - evict clean\n", address);
						evict_page = page;
						page_entry = page_ptr[evict_page];

                                		page_entry->frame = -1;
                                		page_entry->valid = 0;
                                		page_entry->referenced = 0;
						
						/* swap in page */
                                		frame_array[k] = pNum;
                                		page_entry = page_table;
                                		page_entry = page_ptr[pNum];
                                		page_entry->valid = 1;
                                		page_entry->frame = k;
                                		page_entry->referenced = 1;
						break;
					}

					if((page_ptr[page]->dirty == 1) && (page_ptr[page]->referenced == 0))
					{
						//evict page
                                                printf("%x Page fault - evict dirty\n", address);
                                                evict_page = page;
                                                page_entry = page_ptr[evict_page];

                                                page_entry->frame = -1;
                                                page_entry->valid = 0;
                                                page_entry->referenced = 0;

                                                /* swap in page */
                                                frame_array[k] = pNum;
                                                page_entry = page_table;
                                                page_entry = page_ptr[pNum];
                                                page_entry->valid = 1;
                                                page_entry->frame = k;
                                                page_entry->referenced = 1;
						disk_writes++;
						break;
					}

					if((page_ptr[page]->dirty == 0) && (page_ptr[page]->referenced ==1))
					{
						//evict page
                                                printf("%x Page fault - evict clean\n", address);
                                                evict_page = page;
                                                page_entry = page_ptr[evict_page];

                                                page_entry->frame = -1;
                                                page_entry->valid = 0;
                                                page_entry->referenced = 0;

                                                /* swap in page */
                                                frame_array[k] = pNum;
                                                page_entry = page_table;
                                                page_entry = page_ptr[pNum];
                                                page_entry->valid = 1;
                                                page_entry->frame = k;
                                                page_entry->referenced = 1;
						break;
					}

					if((page_ptr[page]->dirty == 1) && (page_ptr[page]->referenced ==1))
					{
						//evict page
                                                printf("%x Page fault - evict dirty\n", address);
                                                evict_page = page;
                                                page_entry = page_ptr[evict_page];

                                                page_entry->frame = -1;
                                                page_entry->valid = 0;
                                                page_entry->referenced = 0;

                                                /* swap in page */
                                                frame_array[k] = pNum;
                                                page_entry = page_table;
                                                page_entry = page_ptr[pNum];
                                                page_entry->valid = 1;
                                                page_entry->frame = k;
                                                page_entry->referenced = 1;
						disk_writes++;
						break;
					}

				}
			}
			

		}
	mem_accesses++;
	}
	printf("%s\n Number of frames: %d\n Total memory accesses: %d\n Total page faults: %d\n Total writes to disk: %d\n", algorithm, num_frames, mem_accesses, page_faults, disk_writes);
}


void sim_aging(int frames)
{

	int curr_line = 0;
        int pNum;
        int evict_page;
        int page_faults = 0;
        int disk_writes = 0;
        int mem_accesses = 0;
        int curr_frame = 0;
	int count_array[num_frames];
        trace_file = fopen(filename, "r");
        struct Page* page_table = malloc(sizeof(struct Page));
        struct Page* page_entry = malloc(sizeof(struct Page));

        page_entry = page_table;
        int k;
        for(k = 0; k < num_pages; k++)
        {
                page_entry->number = k;
                page_entry->valid = 0;
                page_entry->referenced = 0;
                page_entry->dirty = 0;
                page_entry->next = malloc(sizeof(struct Page));
                page_ptr[k] = page_entry;
                page_entry = page_entry->next;
        }

	/* arary holding # of frames */
        int frame_array[num_frames];
        int i;
        for(i = 0; i < num_frames; i++)
                frame_array[i] = -1;

	 while(fscanf(trace_file, "%x %c", &address, &mode) != EOF)
        {
                pNum = address >> 12;
                page_entry = page_ptr[pNum];
                if(mode == 87)          //87 is equal to 'w'
                {
                        page_entry->dirty = 1;
                }

		
		if(mem_accesses % refresh == 0)
                {
                         int i;
                        for(i = 0; i < num_frames; i++)
                        {
                                int page = frame_array[i];
                                if(page == -1)
                                {
                                        break;
                                }
                                else
                                {
                                 	/* update page counters
					 * right shift counter and add rbit to msb position
					 * change all reference bits to 0
					 */
					
					page_ptr[page]->counter = page_ptr[page]->counter >> 1;
					page_ptr[page]->counter = page_ptr[page]->counter |= page_ptr[page]->referenced << 7;     
				}
			}
                }

                curr_line++;

                 if(page_entry->valid == 1)
                        printf("%x hit\n", address);


		else
		{
			 /* Handle page faults by swapping in pages */
                        page_faults++;

                        /* There are available frames */
                        if(curr_frame < num_frames)
                        {
                                page_entry->frame = curr_frame;
                                page_entry->valid = 1;
				page_entry->referenced = 1;
                                frame_array[curr_frame] = page_entry->number;
                                printf("%x Page fault - no eviction\n", address);
                                curr_frame++;
                        }

			else
			{
				/* No avavilable frames so evict based on aging algorithm */
				int x;
				int y;
				for(x = 0; x < num_frames; x++)
				{
					int page_num = frame_array[x];
					count_array[x] = page_ptr[page_num]->counter;
				}

				int lowest = count_array[0];
				x = 0;
				
				for(y = 1; y < num_frames; y++)
				{
					if(count_array[y]<lowest)
					{
						lowest = count_array[y];
						x++;
					}
		
				}

				//evict page
				evict_page = frame_array[x];
				page_entry = page_ptr[evict_page];
				page_entry->frame = -1;
				page_entry->valid = 0;
				page_entry->referenced = 0;

				if(page_entry->dirty == 1)
				{
					printf("%x Page fault - evict dirty\n", address);	
					disk_writes++;
				}

				else
				{
					printf("%x Page fault - evict clean\n", address);

				}	

				
				/* swap in page */
				frame_array[x] = pNum;
				page_entry = page_table;
				page_entry = page_ptr[pNum];
				page_entry->valid = 1;
				page_entry->frame = x;
				page_entry->referenced = 1;
				
				
			}
		}

		mem_accesses++;
	}
	printf("%s\n Number of frames: %d\n Total memory accesses: %d\n Total page faults: %d\n Total writes to disk: %d\n", algorithm, num_frames, mem_accesses, page_faults, disk_writes);

}

int main(int argc, char *argv[])
{
	if(argc<8)
	{
		printf("Enter all the required arguments \n");
		exit(0);
	}	
	
	/* process arguments
	 * -n for number of frames
	 * -a for algorithms
	 * -r for refresh
         */	

	if((strcmp(argv[1], "-n") == 0))
	{
		num_frames = atoi(argv[2]);
	}

	if((strcmp(argv[3], "-a") == 0))
	{
		strcpy(algorithm, argv[4]);
	}

	if((strcmp(argv[5], "-r") == 0))
	{
		refresh = atoi(argv[6]);
	}

	int i;
	for(i = 0; i < num_pages; i++)
	{
		page_array[i]  = malloc(sizeof(struct Node));
	}

	/* Simulate optimal algorithm */
	if(strcmp(algorithm, "opt") == 0)
	{
		trace_file = fopen(argv[7], "r");
		strcpy(filename, argv[7]);
		sim_opt(num_frames);
	}		


	/* Simulate clock algorithm */
	if(strcmp(algorithm, "clock") == 0)
        {
                trace_file = fopen(argv[7], "r");
                strcpy(filename, argv[7]);
                sim_clock(num_frames);
        }

	
	/* Simulate NRU algorithm */
	if(strcmp(algorithm, "nru") == 0)
	{
		trace_file = fopen(argv[7], "r");
                strcpy(filename, argv[7]);
                sim_nru(num_frames);
	}

	/* Simulate aging algorithm */
	if(strcmp(algorithm, "aging") == 0)
        {
                trace_file = fopen(argv[7], "r");
                strcpy(filename, argv[7]);
                sim_aging(num_frames);
        }

	return 0;
}
