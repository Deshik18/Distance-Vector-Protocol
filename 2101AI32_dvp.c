#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <limits.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>

#define MAX_ROUTERS 20

FILE* work;

// Declare a volatile flag to be used in the signal handler
volatile int keep_running = 1;

void signal_handler(int signo) {
    if (signo == SIGINT) {
        printf("\nCtrl+C detected. Cleaning up and exiting.\n");
        keep_running = 0;
    }
}

struct Router {
    char name;
    int num_routers;
    char routers[MAX_ROUTERS];
    int distance[MAX_ROUTERS];
    int dc[MAX_ROUTERS];
    char next_hop[MAX_ROUTERS];
    int nhc[MAX_ROUTERS];
};

struct Message {
    long mtype; // Message type
    struct Router router;
};

struct Router routers[MAX_ROUTERS];
int num_routers;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int message_queues[MAX_ROUTERS];

void init_message_queues() {
    for (int i = 0; i < num_routers; i++) {
        key_t key = ftok(".", i + 1); // Generate a unique key for each message queue
        message_queues[i] = msgget(key, 0666 | IPC_CREAT);
    }
}

void print_routing_table(struct Router* router, int iteration) {
    printf("Routing table for Router %c (Iteration %d):\n", router->name, iteration);
    printf("Destination   Distance   Next Hop\n");
    for (int i = 0; i < num_routers; i++) {
        char q1=' ',q2=' ';
        if(router->dc[i]){
            q1='*';
            router->dc[i]=0;
        }
        if(router->nhc[i]){
            q2='*';
            router->nhc[i]=0;
        }
        printf("     %c          %c%d       %c%c\n", router->routers[i], q1, router->distance[i], q2, router->next_hop[i]);
    }
    printf("\n");
}

int visited[MAX_ROUTERS];

void dfs(int v) {
    visited[v] = 1;
    for (int i = 0; i < num_routers; i++) {
        if (!visited[i] && routers[v].distance[i] != 9999) {
            dfs(i);
        }
    }
}

int is_connected() {
    for (int i = 0; i < num_routers; i++) {
        visited[i] = 0;
    }

    dfs(0); // Start the DFS from the first router

    int connected = 1; // Assume connected
    for (int i = 0; i < num_routers; i++) {
        if (!visited[i]) {
            connected = 0; // If any router is not visited, the graph is disconnected
            break;
        }
    }

    return connected;
}

void send_routing_table(struct Router* router) {
    struct Message msg;
    msg.mtype = 1; // Message type (change if needed)

    for (int i = 0; i < num_routers; i++) {
        if (router->distance[i] != 9999 && router->name != router->routers[i]) {
            msg.router = *router;
            // Send the routing table to the neighbor's message queue
            msgsnd(message_queues[router->routers[i] - 'A'], &msg, sizeof(msg.router), 0);
        }
    }
}

void receive_routing_tables(struct Router* router, int iteration) {
    struct Message msg;

    for (int i = 0; i < num_routers; i++) {
        if (routers[i].name != router->name) {
            // Receive the routing table from a neighbor's message queue
            if (msgrcv(message_queues[router->name - 'A'], &msg, sizeof(msg.router), 1, IPC_NOWAIT) != -1) {
                // Update the router's routing table based on the received information
                int idx = msg.router.name - 'A';
                for (int j = 0; j < num_routers; j++) {
                    if (msg.router.distance[j] + router->distance[idx] < router->distance[j]) {
                        fprintf(work, "Updated %c table of iteration %d message sent by %c\n", router->name, iteration, msg.router.name);    //  router  ---->   jth
                        fprintf(work, "%c -> %c(Initial d=%d)\n", router->name, router->routers[j], router->distance[j]);                    //       \       /
                        fprintf(work, "%c -> %c(Saying d=%d)\n", msg.router.name, router->routers[j], msg.router.distance[j]);               //          msg
                        fprintf(work, "%c -> %c(Saying d=%d)\n", router->name, msg.router.name, router->distance[idx]);
                        router->distance[j] = msg.router.distance[j] + router->distance[idx];
                        router->next_hop[j] = router->next_hop[idx];
                        router->dc[j] = 1;
                        router->nhc[j] = 1;
                        fprintf(work, "%c -> %c(Final d=%d)\n\n", router->name, router->routers[j], router->distance[j]);
                    }
                }
            }
        }
    }
}

void cleanup() {
    // Close and destroy message queues
    for (int i = 0; i < num_routers; i++) {
        msgctl(message_queues[i], IPC_RMID, NULL);
    }

    // Destroy the mutex
    pthread_mutex_destroy(&mutex);

    // Close the report file
    fclose(work);
}

void* router_thread(void* arg) {
    struct Router* router = (struct Router*)arg;

    int iteration = 0;
    while (keep_running) {
        // Wait for 3 seconds before starting the computation round
        //sleep(3);

        // Simulated distance vector calculation (Bellman-Ford)
        pthread_mutex_lock(&mutex);
        // Print the routing table for this router with the iteration number
        print_routing_table(router, iteration);
        iteration++;
        send_routing_table(router);
        pthread_mutex_unlock(&mutex);

        // Wait for 2 seconds before moving to the next computation round
        sleep(2);
        pthread_mutex_lock(&mutex);
        // Update the router's routing table based on the received information
        receive_routing_tables(router, iteration);
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

int main() {
    // Set up the signal handler for SIGINT
    signal(SIGINT, signal_handler);

    // Read the topology from the input file
    FILE* file = fopen("topology.txt", "r");
    work = fopen("report.txt", "w");
    if (file == NULL) {
        perror("Error opening the file");
        return 1;
    }

    fscanf(file, "%d", &num_routers);

    // Read router names and initialize the routers
    for (int i = 0; i < num_routers; i++) {
        fscanf(file, " %c", &routers[i].name);
        routers[i].num_routers = num_routers;
    }

    // Initialize the routers
    for (int i = 0; i < num_routers; i++) {
        for (int j = 0; j < num_routers; j++) {
            routers[i].routers[j] = routers[j].name;
            routers[i].dc[j]=0;
            routers[i].nhc[j]=0;
            if (i != j) {
                routers[i].distance[j] = 9999; // Initialize with a large distance
                routers[i].next_hop[j] = '\0';
            } else {
                routers[i].distance[j] = 0;
                routers[i].next_hop[j] = routers[i].name;
            }
        }
    }

    // Read and populate distance information
    while (!feof(file)) {
        char source, dest;
        int cost;
        if (fscanf(file, " %c %c %d", &source, &dest, &cost) == 3) {
            int source_idx = -1;
            int dest_idx = -1;
            for (int i = 0; i < num_routers; i++) {
                if (routers[i].name == source) {
                    source_idx = i;
                }
                if (routers[i].name == dest) {
                    dest_idx = i;
                }
            }

            if (source_idx != -1 && dest_idx != -1) {
                routers[source_idx].distance[dest_idx] = cost;
                routers[source_idx].next_hop[dest_idx] = routers[dest_idx].name;
                routers[dest_idx].distance[source_idx] = cost;
                routers[dest_idx].next_hop[source_idx] = routers[source_idx].name;
            }
        }
    }
    fclose(file);

    // Check if the graph is connected
    if (!is_connected()) {
        printf("The input topology is disconnected.\n");
        cleanup();
        return 1;
    }

    // Initialize message queues
    init_message_queues();

    // Create threads for router simulation
    pthread_t router_threads[MAX_ROUTERS];
    for (int i = 0; i < num_routers; i++) {
        if (pthread_create(&router_threads[i], NULL, router_thread, &routers[i]) != 0) {
            perror("Error creating router thread");
            cleanup();
            return 1;
        }
    }

    // Wait for router threads to finish (this won't actually happen in this basic simulation)
    for (int i = 0; i < num_routers; i++) {
        if (pthread_join(router_threads[i], NULL) != 0) {
            perror("Error joining router thread");
            cleanup();
            return 1;
        }
    }

    cleanup();
    return 0;
}