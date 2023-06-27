/*TODO:
1. Проверить, что в текущем виде (mqtt_demo запускается в МС) все работает
2. Реализовать систему текста ошибок
3. Сделать, чтобы из ST_NOT_CONNECTED МС стукала wifi или mqtt_demo для подключения
4. (DONE) Передача текущего состояния после выхода из машины состояний
5. (DONE) Состояния перевести в отдельных header
6. Проверка команд топика manual_cmd на неправильные значения flag
7. Отловить проблемы с выводом "Wrong message in msg/pump_cmd" и "Wrong message in msg/tap_cmd"
*/

#include <mbed.h>
#include "MQTTmbed.h"
#include "MQTTClient.h"
#include "MQTTNetwork.h"
#include "states.h"

DigitalOut led(LED1);
DigitalOut tap_output(D10);
DigitalOut pump_output(D4);
DigitalIn water_flow_sensor(D3);
DigitalIn water_level_sensor(A3);
DigitalIn full_water_sensor(A4);
WiFiInterface *wifi;
MQTT::Client<MQTTNetwork, Countdown>* client_p = nullptr;

char* topic_manual_cmd = "msg/manual_cmd";
char* topic_pump_cmd = "msg/pump_cmd";
char* topic_tap_cmd = "msg/tap_cmd";
char* topic_state = "msg/stm_1_state";
char* topic_water_flow = "msg/water_flow";
bool manual_ctrl = 0;
int counter = 0;
bool mqtt_thread_start = 0;

bool TAP_ON = 0;
bool TAP_OFF = 1;
string x;

STATES state = ST_INIT;
STATES previous_state = ST_INIT;
char* hostname = "52.29.79.184";
int port = 1883;

bool pump_cmd = 0;
bool flow_cmd = 0;
bool tap_cmd = 0;
bool tap_cmd_current = 0;
bool water_level = 0;


#define MQTTCLIENT_QOS2 1
 
int arrivedcount = 0;
Thread thread1;
 

void send_msg(char *topic, char *buf)
{
    MQTT::Message message;
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*)buf;
    message.payloadlen = strlen(buf)+1;
    int rc = client_p->publish(topic, message);
}

void messageArrived(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;
    printf("Message arrived: qos %d, retained %d, dup %d, packetid %d\r\n", message.qos, message.retained, message.dup, message.id);
    printf("Payload %.*s\r\n", message.payloadlen, (char*)message.payload);
    ++arrivedcount;
}

void manual_control_topic_listener(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;
    int flag = -1;
    if(sscanf((char*)message.payload, "manual control: %d", &flag) > 0)
    {
        if(flag == 1)
        {
            manual_ctrl = true;
            printf("Manual control is on\r\n");  
        }
        else
        {
            manual_ctrl = false;
            printf("Manual control is off\r\n");
        }
    }
    else if(sscanf((char*)message.payload, "tap control: %d", &flag) > 0)
    {
        tap_cmd = (bool)flag;
        printf("Tap command is %d\r\n", tap_cmd);
    }
    else if(sscanf((char*)message.payload, "pump control: %d", &flag) > 0)
    {
        pump_cmd = (bool)flag;
        printf("Pump command is %d\r\n", pump_cmd);
    }
    else
    {
        printf("Wrong command from msg/manual_ctr");
    }
}

void pump_topic_listener(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;
    int flag;
    if(sscanf((char*)message.payload, "pump: %d", &flag) > 0)
    {
        if(flag == 1)
        {   
            pump_cmd = true;
            printf("Pump is on: %d\r\n", pump_cmd);    
        }
        else if(flag == 0)
        {
            pump_cmd = false;
            printf("Pump is off: %d\r\n", pump_cmd);
        }
        else
        {
            printf("Wrong command for pump\r\n");
        }
    }
    else
    {
        printf("Wrong message in msg/pump_cmd\r\n");
    }
}

void tap_listener(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;
    int flag;
    if(sscanf((char*)message.payload, "tap: %d", &flag) > 0)
    {
        if(flag == 1)
        {   
            tap_cmd = true;
            printf("Tap is on: %d\r\n", tap_cmd);    
        }
        else if(flag == 0)
        {
            tap_cmd = false;
            printf("Tap is off: %d\r\n", tap_cmd);
        }
        else
        {
            printf("Wrong command for tap\r\n");
        }
    }
    else
    {
        printf("Wrong message in msg/tap_cmd\r\n");
    }
    /*sscanf((char*)message.payload, "tap: %d", &tap_cmd);
    if(tap_cmd_current!=tap_cmd)
    {
        if(tap_cmd)
        {
            printf("Tap is open: %d\r\n", tap_cmd); 
        }
        else
        {
            printf("Tap is close: %d\r\n", tap_cmd);
        }
    }
    tap_cmd_current = tap_cmd;*/
}

void flow_water_listener(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;
    sscanf((char*)message.payload, "flow: %d", &flow_cmd);

    if(flow_cmd)
    {
        printf("Water is flowing: %d\r\n", flow_cmd);    
    }
    else
    {
        printf("Water is not flowing: %d\r\n", flow_cmd);
    }
}

void water_flow_listener(MQTT::MessageData& md)
{
   MQTT::Message &message = md.message;
    sscanf((char*)message.payload, "tap: %d", &water_level);
    if(water_level)
    {
        printf("Level is low: %d\r\n", water_level);    
    }
    else
    {
        printf("Level is full: %d\r\n", water_level);
    }
}

void mqtt_demo(NetworkInterface *net)
{
    if(mqtt_thread_start)
    {
        float version = 0.6;

        MQTTNetwork network(net);
        client_p = new MQTT::Client<MQTTNetwork, Countdown>(network);

        printf("Connecting to %s:%d\r\n", hostname, port);

        int rc = network.connect(hostname, port);
    
        if (rc != 0)
        {
            printf("rc from TCP connect is %d\r\n", rc);
            printf("Connected socket\n\r");
        }
        else
        {
            state = ST_ERROR;
        }
    
        MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
        data.MQTTVersion = 3;

        data.clientID.cstring = "2a617592b917";
        //data.username.cstring = "barrychef";
        //data.password.cstring = "goodcoocking";
        
        if ((rc = client_p->connect(data)) != 0)
            printf("rc from MQTT connect is %d\r\n", rc);
        if ((rc = client_p->subscribe(topic_manual_cmd, MQTT::QOS2, manual_control_topic_listener)) != 0)
            printf("rc from MQTT subscribe is %d\r\n", rc);
        if ((rc = client_p->subscribe(topic_water_flow, MQTT::QOS2, flow_water_listener)) != 0)
            printf("rc from MQTT subscribe is %d\r\n", rc);
        if((rc = client_p->subscribe(topic_state, MQTT::QOS2, messageArrived)) != 0)
            printf("rc from MQTT subscribe is %d\r\n", rc);
        if((rc = client_p->subscribe(topic_pump_cmd, MQTT::QOS2, pump_topic_listener)) != 0)
            printf("rc from MQTT subscribe is %d\r\n", rc);
        if((rc = client_p->subscribe(topic_tap_cmd, MQTT::QOS2, tap_listener)) != 0)
            printf("rc from MQTT subscribe is %d\r\n", rc);
            
        //MQTT::Message message;
        // QoS 0
        /*char buf[100];
        message.qos = MQTT::QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void*)buf;
        message.payloadlen = strlen(buf)+1;
        rc = client_p->publish(topic_manual_cmd, message);*/
        printf("Version %.2f: finish %d msgs\r\n", version, arrivedcount);
        
        while (true)
            client_p->yield(100);
    }
}

void arrive_msg(NetworkInterface *net)
{
    MQTTNetwork network(net);
    MQTT::Client<MQTTNetwork, Countdown> client = MQTT::Client<MQTTNetwork, Countdown>(network);
    while (true)
        client.yield(100);
}

const char *sec2str(nsapi_security_t sec)
{
    switch (sec) 
    {
        case NSAPI_SECURITY_NONE:
            return "None";
        case NSAPI_SECURITY_WEP:
            return "WEP";
        case NSAPI_SECURITY_WPA:
            return "WPA";
        case NSAPI_SECURITY_WPA2:
            return "WPA2";
        case NSAPI_SECURITY_WPA_WPA2:
            return "WPA/WPA2";
        case NSAPI_SECURITY_UNKNOWN:
        default:
            return "Unknown";
    }
}
int scan_demo(WiFiInterface *wifi)
{
    WiFiAccessPoint *ap;

    printf("Scan:\n");

    int count = wifi->scan(NULL,0);

    if (count <= 0) {
        printf("scan() failed with return value: %d\n", count);
        return 0;
    }

    /* Limit number of network arbitrary to 15 */
    count = count < 15 ? count : 15;

    ap = new WiFiAccessPoint[count];
    count = wifi->scan(ap, count);

    if (count <= 0) {
        printf("scan() failed with return value: %d\n", count);
        return 0;
    }

    for (int i = 0; i < count; i++) 
    {
        printf("Network: %s secured: %s BSSID: %hhX:%hhX:%hhX:%hhx:%hhx:%hhx RSSI: %hhd Ch: %hhd\n", ap[i].get_ssid(),
               sec2str(ap[i].get_security()), ap[i].get_bssid()[0], ap[i].get_bssid()[1], ap[i].get_bssid()[2],
               ap[i].get_bssid()[3], ap[i].get_bssid()[4], ap[i].get_bssid()[5], ap[i].get_rssi(), ap[i].get_channel());
    }
    printf("%d networks available.\n", count);

    delete[] ap;

    return count;
}

int main()
{    
    state = ST_CONNECTING;
    tap_output = TAP_OFF;
    printf("WiFi example\n");
    #ifdef MBED_MAJOR_VERSION
        printf("Mbed OS version %d.%d.%d\n\n", MBED_MAJOR_VERSION, MBED_MINOR_VERSION, MBED_PATCH_VERSION);
    #endif

    wifi = WiFiInterface::get_default_instance();

    if (!wifi) 
    {
        printf("ERROR: No WiFiInterface found.\n");
        state = ST_ERROR;
    }
    else
    {
        state = ST_CONNECTING;
    }

    printf("\nDone\n");

    while(true)
    {
        switch(state)
        {
            case ST_INIT:
            {
                if(previous_state != state)
                {
                    send_msg(topic_state, "ST_INIT");
                    previous_state = state;
                }
                state = ST_CONNECTING;
                break;
            }

            case ST_CONNECTING:
            {
                int count = scan_demo(wifi);

                if (count == 0) 
                {
                    printf("No WIFI APs found - can't continue further.\n");
                    state = ST_ERROR;
                }
                else
                {
                    printf("\nConnecting to %s...\n", MBED_CONF_APP_WIFI_SSID);
                    int ret = wifi->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
                    if (ret != 0)
                    {
                        printf("\nConnection error: %d\n", ret);
                        state = ST_ERROR;
                    }
                    else
                    {
                        printf("Success\n\n");
                        printf("MAC: %s\n", wifi->get_mac_address());
                        SocketAddress a;
                        wifi->get_ip_address(&a);
                        printf("IP: %s\n", a.get_ip_address());
                        wifi->get_netmask(&a);
                        printf("Netmask: %s\n", a.get_ip_address());
                        wifi->get_gateway(&a);
                        printf("Gateway: %s\n", a.get_ip_address());
                        printf("RSSI: %d\n\n", wifi->get_rssi());
                        state = ST_CONNECTED;
                    }
                    if(previous_state != state)
                    {
                        send_msg(topic_state, "ST_CONNECTING");
                        previous_state = state;
                    }
                
                }
                break;
            }

            case ST_CONNECTED:
            {
                if(!mqtt_thread_start)
                {
                    thread1.start([&](){
                        mqtt_demo(wifi);
                    });
                    mqtt_thread_start = 1;
                    thread_sleep_for(5000);
                }

                if(previous_state != state)
                    {
                        send_msg(topic_state, "ST_CONNECTED");
                        previous_state = state;
                    }
                state = ST_READY;
                break;
            }

            case ST_READY:
            {
                if(previous_state != state)
                {
                    send_msg(topic_state, "ST_READY");
                    previous_state = state;
                }
                if(water_level_sensor.read() == 0 && tap_cmd == 1)
                {
                    state = ST_PUMPING;
                }
                
                if(water_level_sensor.read() == 1 && tap_cmd == 1)
                {
                    state = ST_WATER_FEED;
                }
                break;
            }

            case ST_PUMPING:
                if(previous_state != state)
                {
                    send_msg(topic_state, "ST_PUMPING");
                    previous_state = state; 
                }
                pump_output = 1;
                //x = water_flow_sensor.read();
                //printf("st", x);
                //thread_sleep_for(2000);
                // if(water_flow_sensor.read() == 0)
                // {
                //     //state = ST_ERROR;
                //     if(previous_state != state)
                //     {
                //         send_msg(topic_state, "ST_ERROR");
                //         previous_state = state;
                //     }
                // }
                if(full_water_sensor.read() == 1) 
                {
                    pump_output = 0;
                    state = ST_READY;
                }
                break;

            case ST_WATER_FEED:
                 if(previous_state != state)
                {
                    send_msg(topic_state, "ST_WATER_FEED");
                    previous_state = state;
                }
                tap_output = TAP_ON;
                if(!tap_cmd)
                {
                    tap_output = TAP_OFF;
                    state = ST_READY;
                }

                if(water_level_sensor.read() == 0)
                {
                    tap_output = TAP_OFF; 
                    state = ST_PUMPING;
                }
                break;

            case ST_NOT_CONNECTED: //дописать состояние так, чтобы он тыкался переподключаться
                printf("Error");
                if(previous_state != state)
                {
                    send_msg(topic_state, "ST_NOT_CONNECTED");
                    previous_state = state;
                }
                break;
            
            case ST_MANUAL_CONTROL:
                
                if(previous_state != state)
                {
                    send_msg(topic_state, "ST_MANUAL_CONTROL");
                    previous_state = state;
                }
                if(!manual_ctrl)
                {
                    state = ST_READY;
                }
                else
                {
                pump_output = pump_cmd;
                tap_output = tap_cmd;
                }
                break;

            case ST_ERROR:
            if(previous_state != state)
                {
                    send_msg(topic_state, "ST_ERROR");
                    previous_state = state;
                }
                break;      
        }
        if(manual_ctrl)
        {
            state = ST_MANUAL_CONTROL;
        }
     }
}