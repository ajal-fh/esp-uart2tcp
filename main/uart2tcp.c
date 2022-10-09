/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>


//headers for uart
#include "driver/uart.h"
#include "driver/gpio.h"

#define PORT                        CONFIG_EXAMPLE_PORT
#define KEEPALIVE_IDLE              CONFIG_EXAMPLE_KEEPALIVE_IDLE
#define KEEPALIVE_INTERVAL          CONFIG_EXAMPLE_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT             CONFIG_EXAMPLE_KEEPALIVE_COUNT

//--------------------WIFI AP code----------------------------//
#define EXAMPLE_ESP_WIFI_SSID      "myrobot"
#define EXAMPLE_ESP_WIFI_PASS      "robocop123"
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN
//-------------------------------------------------------------//
static const char *TAG = "uart2tcp";
TaskHandle_t tcp_sender_handle;
TaskHandle_t uart_reciever_handle;
#define PACKET_SIZE_BYTES 12   // it is expected that the uart sender already packs the data in specified format to send over tcp, change based on your data
#define Q_SIZE 5               // the number of packets our queue can hold
QueueHandle_t msg_buffer;
SemaphoreHandle_t Semaphore_TCP; // we need a shared variable which tells the uart reciever task if the tcp has any clients or not
static volatile bool tcp_client_connected = false;
//----------------------------------------------------------------------//

#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_16)
//---------------------------------------------------------------------//
void init_uart(){
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    // We won't use a buffer for sending data.
    //uart_driver_install(UART_NUM_2, PACKET_SIZE_BYTES * 2, 0, 0, NULL, 0);
    uart_driver_install(UART_NUM_2, 1024 * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_2, &uart_config);
    uart_set_pin(UART_NUM_2, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void my_uart_loop(){
    //int count = 0;
    //char msg[] = "Hello World\n";
    bool is_client_connected = false;
    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *) malloc(PACKET_SIZE_BYTES + 1);
    init_uart();
    while(1){
        // check if tcp client connected if not just drop the data from uart
        // this way we can ensure that the queue contains latest values
        xSemaphoreTake(Semaphore_TCP, portMAX_DELAY);
        is_client_connected = tcp_client_connected;
        xSemaphoreGive(Semaphore_TCP);
        if(is_client_connected){
            // Read data from the UART
            int len = uart_read_bytes(UART_NUM_2, data, PACKET_SIZE_BYTES, 1000 / portTICK_RATE_MS);
            if(len){
                ESP_LOGI(TAG, "Sending data from uart");
                xQueueSend(msg_buffer, data, portMAX_DELAY);
                //vTaskDelay(1000 / portTICK_RATE_MS);
            }
            
            //count++;
        }
        else{
            //count = 0;
            ESP_LOGW(TAG, "Dropping packet since no client connected");
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
        
    }
    free(data);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    //sends esp to frenzy
    //ESP_ERROR_CHECK(esp_event_loop_create_default()); 
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

static void do_transmit(const int sock)
{
    int len;
    char rx_buffer[PACKET_SIZE_BYTES];
    //char rx_buffer[] = "Hello World";
    len = sizeof(rx_buffer);
    //int my_count;
    //len=sizeof(my_count);
    do {
        xQueueReceive(msg_buffer, &rx_buffer, portMAX_DELAY);
        //xQueueReceive(msg_buffer, &my_count, portMAX_DELAY);
        /*
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);
        */
            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation.
            int to_write = len;
            while (to_write > 0) {
                int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
                //int written = send(sock, &my_count, to_write, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    
                    return;
                }
                to_write -= written;
            }
        
    } while (len > 0);
    
    return;
}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }
#ifdef CONFIG_EXAMPLE_IPV6
    else if (addr_family == AF_INET6) {
        struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&dest_addr;
        bzero(&dest_addr_ip6->sin6_addr.un, sizeof(dest_addr_ip6->sin6_addr.un));
        dest_addr_ip6->sin6_family = AF_INET6;
        dest_addr_ip6->sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;
    }
#endif

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        //Make uart task know that a client is connected

        xSemaphoreTake(Semaphore_TCP, portMAX_DELAY);
        tcp_client_connected = true;
        xSemaphoreGive(Semaphore_TCP);

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
#ifdef CONFIG_EXAMPLE_IPV6
        else if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
#endif
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        do_transmit(sock);
        
        xSemaphoreTake(Semaphore_TCP, portMAX_DELAY);
        tcp_client_connected = false;
        xSemaphoreGive(Semaphore_TCP);
        
        // make sure the queue is empty for the new connection
        //xQueueReset(msg_buffer);
        shutdown(sock, 0);
        close(sock);
        
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

/*void dummyprint(void *pvParameters){
    while (1){
        xDelay
        ESP_LOGI(TAG, "dummy task running");
    }
    
}*/

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // wifi ap code
    //Initialize NVS
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

    // setup the queue which can hold 5 packets
    char dummy_buffer[PACKET_SIZE_BYTES];
    size_t buffer_size = sizeof(dummy_buffer) / sizeof(dummy_buffer[0]);
    msg_buffer = xQueueCreate(Q_SIZE, buffer_size);
    //msg_buffer = xQueueCreate(Q_SIZE, sizeof(int));
    //setup semaphore
    Semaphore_TCP = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(
        my_uart_loop, /* Function to implement the task*/
        "uart handler", /*Name of the task*/
        4096, /*Stack size in words*/
        NULL, /*task input parameter*/
        0, /*Priority of the task*/
        &uart_reciever_handle, /*Task handle */
        1 /* core where the task would run*/
    );

    xTaskCreatePinnedToCore(
        tcp_server_task,
        "tcp_server",
        4096,
        (void*)AF_INET,
        0,
        &tcp_sender_handle,
        1
    );


}


