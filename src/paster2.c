#include "../inc/lab_png.h"
#include "../inc/crc.h"
#include "../inc/catpng.h"
#include "../inc/paster2.h"

#define SEGMENT_SIZE 10000
#define MAX_SEGMENTS 50
#define ECE252_HEADER "X-Ece252-Fragment: "

typedef struct recv_buf
{
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

// Shared memory structure
typedef struct
{
    // Ring buffer
    char segments[50][SEGMENT_SIZE];
    int segment_nums[50];
    size_t segment_sizes[50];
    int write_idx;
    int read_idx;
    int count;
    int buffer_capacity;

    // Tracking what has been done so far
    int downloaded[MAX_SEGMENTS];
    int total_downloaded;
    int segments_consumed;
    int producers_done;

    // Semaphores
    sem_t mutex;
    sem_t empty;
    sem_t filled;

    // Processed PNG segments
    char processed_segments[MAX_SEGMENTS][SEGMENT_SIZE];
    size_t processed_sizes[MAX_SEGMENTS];
} shared_data_t;

// Function declarations
size_t header_cb_curl2(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl2(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int recv_buf_init2(RECV_BUF *ptr, size_t max_size);
int download_segment(const char *url, RECV_BUF *recv_buf);
void process_png_segment(char *png_data, size_t data_size, int seg_num, shared_data_t *shm);
void producer_process(shared_data_t *shm, int N, int P, int C);
void consumer_process(shared_data_t *shm, int X);
int write_file(const char *path, const void *in, size_t len);

// cURL callbacks
size_t header_cb_curl2(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)userdata;

    if (realsize > strlen(ECE252_HEADER) &&
        strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0)
    {
        p->seq = atoi(p_recv + strlen(ECE252_HEADER));
    }
    return realsize;
}

size_t write_cb_curl2(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;

    if (p->size + realsize + 1 > p->max_size)
    {
        fprintf(stderr, "Buffer overflow\n");
        exit(0);
    }

    memcpy(p->buf + p->size, p_recv, realsize);
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}

// Initialize buf to receive data
int recv_buf_init2(RECV_BUF *ptr, size_t max_size)
{
    if (ptr == NULL)
    {
        return 1;
    }

    ptr->buf = malloc(max_size);
    if (ptr->buf == NULL)
    {
        return 1;
    }

    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1; // valid seq will be non negative

    return 0;
}

// using curl to download each segment
int download_segment(const char *url, RECV_BUF *recv_buf)
{
    CURL *curl_handle;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_handle = curl_easy_init();

    if (curl_handle == NULL)
    {
        curl_global_cleanup();
        return 1;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl2);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)recv_buf);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl2);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)recv_buf);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    res = curl_easy_perform(curl_handle);
    if (res != CURLE_OK)
        return 1;

    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    return 0;
}

// another helper function to store the png segments in a critical section
void process_png_segment(char *png_data, size_t data_size, int seg_num, shared_data_t *shm)
{
    // Consumer just validates and stores
    sem_wait(&shm->mutex);
    memcpy(shm->processed_segments[seg_num], png_data, data_size);
    shm->processed_sizes[seg_num] = data_size;
    sem_post(&shm->mutex);
}

// the producer process helper
void producer_process(shared_data_t *shm, int N, int P, int C)
{
    while (1)
    {
        int segment = -1;

        // Get next segment to download
        sem_wait(&shm->mutex);

        for (int i = 0; i < MAX_SEGMENTS; i++)
        {
            if (shm->downloaded[i] == 0)
            {
                shm->downloaded[i] = 1;
                segment = i;
                break;
            }
        }

        sem_post(&shm->mutex);

        if (segment == -1)
        {
            break;
        }

        // Download segment
        RECV_BUF recv_buf;
        if (recv_buf_init2(&recv_buf, SEGMENT_SIZE) != 0)
        {
            sem_wait(&shm->mutex);
            shm->downloaded[segment] = 0;
            sem_post(&shm->mutex);
            continue;
        }

        char url[256];
        sprintf(url, "http://ece252-1.uwaterloo.ca:2530/image?img=%d&part=%d", N, segment);

        if (download_segment(url, &recv_buf) != 0)
        {
            free(recv_buf.buf);
            sem_wait(&shm->mutex);
            shm->downloaded[segment] = 0; // this is so that we can retry this segment
            sem_post(&shm->mutex);
            continue;
        }

        // Wait for space and add to buffer
        sem_wait(&shm->empty);
        sem_wait(&shm->mutex);

        memcpy(shm->segments[shm->write_idx], recv_buf.buf, recv_buf.size);
        shm->segment_nums[shm->write_idx] = segment;
        shm->segment_sizes[shm->write_idx] = recv_buf.size;
        shm->write_idx = (shm->write_idx + 1) % shm->buffer_capacity;
        shm->count++;
        shm->total_downloaded++;

        sem_post(&shm->mutex);
        sem_post(&shm->filled);

        free(recv_buf.buf);
    }

    // Signal that this producer is done
    sem_wait(&shm->mutex);
    shm->producers_done++;

    if (shm->producers_done == P)
    {
        for (int i = 0; i < C; i++)
        {
            sem_post(&shm->filled); // wake up all the waiting consumers
        }
    }
    sem_post(&shm->mutex);
}

void consumer_process(shared_data_t *shm, int X)
{
    while (1)
    {
        sem_wait(&shm->filled);
        sem_wait(&shm->mutex);

        if (shm->segments_consumed >= MAX_SEGMENTS)
        {
            sem_post(&shm->mutex);
            break;
        }

        if (shm->count == 0)
        {
            if (shm->total_downloaded >= MAX_SEGMENTS)
            {
                sem_post(&shm->mutex);
                sem_post(&shm->filled);
                break;
            }
            else
            {
                sem_post(&shm->mutex);
                continue;
            }
        }

        // Get segment from buffer
        char segment_data[SEGMENT_SIZE];
        int segment_num = shm->segment_nums[shm->read_idx];
        size_t segment_size = shm->segment_sizes[shm->read_idx];

        memcpy(segment_data, shm->segments[shm->read_idx], segment_size);

        shm->read_idx = (shm->read_idx + 1) % shm->buffer_capacity;
        shm->count--;
        shm->segments_consumed++;

        sem_post(&shm->mutex);
        sem_post(&shm->empty);

        // Sleep X milliseconds
        usleep(X * 1000);

        // Process segment (just storing it here)
        process_png_segment(segment_data, segment_size, segment_num, shm);
    }
}

int write_file(const char *path, const void *in, size_t len)
{
    FILE *fp = NULL;

    if (path == NULL)
    {
        fprintf(stderr, "write_file: file name is null!\n");
        return -1;
    }

    if (in == NULL)
    {
        fprintf(stderr, "write_file: input data is null!\n");
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL)
    {
        perror("fopen");
        return -2;
    }

    if (fwrite(in, 1, len, fp) != len)
    {
        fprintf(stderr, "write_file: incomplete write!\n");
        fclose(fp);
        return -3;
    }
    return fclose(fp);
}

int main(int argc, char *argv[])
{
    if (argc != 6)
    {
        fprintf(stderr, "Usage: %s <B> <P> <C> <X> <N>\n", argv[0]);
        return 1;
    }

    int B = atoi(argv[1]);
    int P = atoi(argv[2]);
    int C = atoi(argv[3]);
    int X = atoi(argv[4]);
    int N = atoi(argv[5]);

    if (B < 1 || B > 50 || P < 1 || P > 20 || C < 1 || C > 20 ||
        X < 0 || X > 1000 || N < 1 || N > 3)
    {
        fprintf(stderr, "Invalid arguments\n");
        return 1;
    }

    // Start timing 
    struct timeval tv_start, tv_end;
    if (gettimeofday(&tv_start, NULL) != 0)
    {
        perror("gettimeofday");
        abort();
    }

    // Create shared memory
    int shm_size = sizeof(shared_data_t);
    int shmid = shmget(IPC_PRIVATE, shm_size, IPC_CREAT | IPC_EXCL | 0600);

    if (shmid == -1)
    {
        perror("shmget");
        return 1;
    }

    shared_data_t *shm = (shared_data_t *)shmat(shmid, NULL, 0);

    if (shm == (void *)-1)
    {
        perror("shmat");
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    // Initialize IPC
    memset(shm, 0, sizeof(shared_data_t));
    shm->buffer_capacity = B;
    shm->producers_done = 0;

    sem_init(&shm->mutex, 1, 1);
    sem_init(&shm->empty, 1, B);
    sem_init(&shm->filled, 1, 0);

    // Fork producers
    for (int i = 0; i < P; i++)
    {
        pid_t pid = fork();

        if (pid == 0)
        {
            producer_process(shm, N, P, C);
            shmdt(shm);
            exit(0);
        }
        else if (pid < 0)
        {
            perror("fork producer");
            return 1;
        }
    }

    // Fork consumers
    for (int i = 0; i < C; i++)
    {
        pid_t pid = fork();

        if (pid == 0)
        {
            consumer_process(shm, X);
            shmdt(shm);
            exit(0);
        }
        else if (pid < 0)
        {
            perror("fork consumer");
            return 1;
        }
    }

    // Wait for all children
    for (int i = 0; i < (P + C); i++)
    {
        wait(NULL);
    }

    // Write segments to temporary files
    char **filenames = malloc(MAX_SEGMENTS * sizeof(char *));
    for (int i = 0; i < MAX_SEGMENTS; i++)
    {
        filenames[i] = malloc(64);
        sprintf(filenames[i], "strip_%02d.png", i);

        if (write_file(filenames[i], shm->processed_segments[i], shm->processed_sizes[i]) != 0)
        {
            fprintf(stderr, "Failed to write strip %d\n", i);
            return -1;
        }
    }

    // Build argv-style array for catpng
    char **args = malloc((MAX_SEGMENTS + 1) * sizeof(char *));
    args[0] = "./catpng"; // dummy program name
    for (int i = 0; i < MAX_SEGMENTS; i++)
    {
        args[i + 1] = filenames[i];
    }

    // Call catpng to assemble final image
    catpng(MAX_SEGMENTS + 1, args);

    free(args);

    // Clean up temporary files
    for (int i = 0; i < MAX_SEGMENTS; i++)
    {
        if (filenames[i])
        {
            unlink(filenames[i]); // remove temp file
            free(filenames[i]);   // free filename string
        }
    }
    free(filenames);

    // End timing 
    if (gettimeofday(&tv_end, NULL) != 0)
    {
        perror("gettimeofday");
        return 1;
    }

    // Calculate time
    double times[2];
    times[0] = (tv_start.tv_sec) + tv_start.tv_usec / 1000000.;
    times[1] = (tv_end.tv_sec) + tv_end.tv_usec / 1000000.;

    printf("paster2 execution time: %.2f seconds\n", times[1] - times[0]);

    // Cleanup shared memory
    sem_destroy(&shm->mutex);
    sem_destroy(&shm->empty);
    sem_destroy(&shm->filled);
    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}