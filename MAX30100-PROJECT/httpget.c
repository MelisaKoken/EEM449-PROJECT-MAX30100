#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// XDCtools Header files
#include <xdc/runtime/Error.h>
#include <xdc/runtime/System.h>
#include <xdc/std.h>

/* TI-RTOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Swi.h>
#include <ti/sysbios/knl/Queue.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Idle.h>
#include <ti/sysbios/knl/Mailbox.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/drivers/GPIO.h>
#include <ti/net/http/httpcli.h>
#include <ti/drivers/I2C.h>

#include "Board.h"
#include "I2C.h"
#include <time.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#define SOCKETTEST_IP     "192.168.1.31"
#define TIMEIP            "132.163.96.5"

extern Swi_Handle swi0;
extern Mailbox_Handle mailbox0;  //Heartbeat
extern Mailbox_Handle mailbox1;  //NTP

extern Semaphore_Handle semaphore0;
extern Semaphore_Handle semaphore1;
extern Semaphore_Handle semaphore2;
extern Semaphore_Handle semaphore3;

I2C_Handle      i2c;
I2C_Params      i2cParams;
I2C_Transaction i2cTransaction;

char takenTime[20];
char  tempstr[20];

uint8_t ctr;
time_t curtime;

void calibFunc1(void);
void recvTimeFromNTP(char *serverIP, int serverPort, char *data, int size);

Void Timer_ISR(UArg arg1)
{
   Swi_post(swi0);
}

Void swi(UArg arg1){

    Semaphore_post(semaphore0);
    Semaphore_post(semaphore1);

}

Void TaskFunc(UArg arg1, UArg arg2)
{
  while(1){

        Semaphore_pend(semaphore0, BIOS_WAIT_FOREVER);
        curtime  = takenTime[0]*16777216 +  takenTime[1]*65536 + takenTime[2]*256 + takenTime[3] + 60*60*3;
        curtime += ctr++;
        Mailbox_post(mailbox1, &curtime, BIOS_NO_WAIT);

  }
}

void printError(char *errString, int code)
{
    System_printf("Error! code = %d, desc = %s\n", code, errString);
    BIOS_exit(code);
}

Void taskFunc0(UArg arg0, UArg arg1)
{
    Semaphore_pend(semaphore2, BIOS_WAIT_FOREVER);

    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;                 // minimum speed first
    i2c = I2C_open(Board_I2C0, &i2cParams);

    if (i2c == NULL) {
         System_abort("Error Initializing I2C\n");   // error, retval is false
    }
    else {
        calibFunc1();
    }
    I2C_close(i2c);
}

void calibFunc1(void){

        char buf[10];
        char mode;
        int value;
        int cnt = 0;
        int pulse = 0;
        int Pulsecount = 0;
        int heartVal=0;
        int HR;
        int heartBeat = 0;
        int dcFiltered, filteredPrevious,previousHeartRate = 0;
        int previousBw,bwFiltered = 0;

        //Get Part ID
        IIC_readReg(0x57, 0xFF, 1, buf);

        //reset interrupts
        IIC_writeReg(0x57, 0x01,0x00);

        //reset mode
        IIC_writeReg(0x57, 0x06, 0x00);

        //Mode configuration SPO2 Enabled
        IIC_readReg(0x57, 0x06, 1, buf);
        mode = (buf[0] & 0xF8) | 0x03;
        IIC_writeReg(0x57, 0x06, mode);

        //SPO2 Configuration 600 sample
        IIC_readReg(0x57, 0x07, 1, buf);
        mode = (buf[0] & 0xFC) | 0x03;
        IIC_writeReg(0x57,0x07,mode);

        // ADC Resolution 140 bit,100Hz sample rate  is 0x01
        IIC_readReg(0x57, 0x07, 1, buf);
        mode=(buf[0] & 0xe3) | (0x01 << 2);
        IIC_writeReg(0x57,0x07,mode);

        //LED Current select 50mA
        mode = ( 0x08 << 4) | (0x0F) ;
        IIC_writeReg(0x57,0x09,mode);

        //Enable Interrupts
        IIC_writeReg(0x57, 0x01,0xF0); // 1 0 0 0

        while(1) {

              IIC_readReg(0x57, 0x05, 4, buf); //data register
              heartVal = (buf[0] << 8) | buf[1];

              /* DC FILTER */
              dcFiltered = heartVal + (0.75 * filteredPrevious);
              heartVal = dcFiltered - filteredPrevious;
              filteredPrevious = dcFiltered;
              value = heartVal;

              /*BUTTERWORTH FILTER*/
              bwFiltered = (2.452372752527856026e-1 * value) + (0.50952544949442879485 *previousBw);
              previousBw = bwFiltered;
              value = bwFiltered;

              /*HEART BEATS*/
              if(value > previousHeartRate & heartBeat == 0){
                 heartBeat = 1;
              }
              if(value <= previousHeartRate-20 & heartBeat == 1){
                 pulse = Pulsecount;
                 heartBeat = 0;
                 Pulsecount = 0;
              }
              if (cnt==20){
                 cnt=0;
              }

              cnt++;
              Pulsecount++;
              previousHeartRate = value;
              HR = 1200 / pulse;
              Mailbox_post(mailbox0, &HR, BIOS_NO_WAIT);

        }

}

bool sendData2Server(char *serverIP, int serverPort, char *data, int size)
{
    int sockfd, connStat, numSend;
    bool retval=false;
    struct sockaddr_in serverAddr;

    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) {
        System_printf("Socket not created");
        close(sockfd);
        return false;
    }

    memset(&serverAddr, 0, sizeof(serverAddr));  // clear serverAddr structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);     // convert port # to network order
    inet_pton(AF_INET, serverIP, &(serverAddr.sin_addr));

    connStat = connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if(connStat < 0) {
        System_printf("sendData2Server::Error while connecting to server\n");
    }
    else {
        numSend = send(sockfd, data, size, 0);       // send data to the server
        if(numSend < 0) {
            System_printf("sendData2Server::Error while sending data to server\n");
        }
        else {
            retval = true;      // we successfully sent the temperature string
        }
    }
    System_flush();
    close(sockfd);
    return retval;
}

Void clientSocketTask(UArg arg0, UArg arg1)
{
    while(1) {
        // wait for the semaphore that httpTask() will signal
        Semaphore_pend(semaphore3, BIOS_WAIT_FOREVER);

        GPIO_write(Board_LED0, 1); // turn on the LED

        if(sendData2Server(SOCKETTEST_IP, 5011, tempstr, strlen(tempstr))) {
            System_printf("clientSocketTask:: Temperature is sent to the server\n");
            System_flush();
        }

        GPIO_write(Board_LED0, 0);  // turn off the LED
    }
}

void getTimeStr(char *str)
{
    strcpy(str, "2022-01-07 12:34:56");
}

Void serverSocketTask(UArg arg0, UArg arg1)
{
    uint8_t hrValue, averageHr;
    float newHR;
    int serverfd, new_socket, valread, len;
    struct sockaddr_in serverAddr, clientAddr;
    char buffer[30];
    char outstr[30], tmpstr[30];
    bool quit_flag;

    serverfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); //creating socket
    if (serverfd == -1) {
        System_printf("serverSocketTask::Socket not created.. quiting the task.\n");
        return;
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(5030);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // Attaching socket to the port
    if (bind(serverfd, (struct sockaddr *)&serverAddr,  sizeof(serverAddr))<0) {

        System_printf("serverSocketTask::bind failed..\n");
        return;
    }
    if (listen(serverfd, 3) < 0) {

        System_printf("serverSocketTask::listen() failed\n");
        return;
    }

    while(1) {

        len = sizeof(clientAddr);
        if ((new_socket = accept(serverfd, (struct sockaddr *)&clientAddr, &len))<0) {
            System_printf("serverSocketTask::accept() failed\n");
            continue;               // get back to the beginning of the while loop
        }

        System_printf("Accepted connection\n"); // IP address is in clientAddr.sin_addr
        System_flush();

        quit_flag = false;
        do {

            if((valread = recv(new_socket, buffer, 15, 0))<0) {
                close(new_socket);
                break;
            }

            buffer[15]=0;
            if(valread<15) buffer[valread]=0;

            System_printf("Received Message: %s\n", buffer);

            if(!strcmp(buffer, "TIME")) {
               time_t getTime;
               getTimeStr(tmpstr);
               Semaphore_post(semaphore0);
               Semaphore_post(semaphore0);
               Mailbox_pend(mailbox1, &getTime, BIOS_WAIT_FOREVER);
               Mailbox_pend(mailbox1, &getTime, BIOS_WAIT_FOREVER);
               sprintf(outstr, " TIME: %s\n", ctime(&getTime));
               send(new_socket , outstr , strlen(outstr) , 0);
            }
            else if(!strcmp(buffer, "READ HEARTBEAT")) {
                uint16_t hrValueTotal=0;
                int i;
                for (i = 0; i < 10; i++) {

                Semaphore_post(semaphore2);
                Mailbox_pend(mailbox0, &hrValue, BIOS_WAIT_FOREVER);
                hrValueTotal+= hrValue ;

                }
                averageHr = hrValueTotal/10;
                newHR = (float)(averageHr);

                sprintf(outstr, " HEART BEAT IS =  %5.2f\n", newHR);
                send(new_socket , outstr , strlen(outstr) , 0);
            }
            else if(!strcmp(buffer, "SHUTDOWN")) {
                quit_flag = true;     // it will allow us to get out of while loop
                strcpy(outstr, "  SYSTEM IS CLOSED !!");
                send(new_socket , outstr , strlen(outstr) , 0);
            }

        }
        while(!quit_flag);

        System_flush();
        close(new_socket);
        BIOS_exit(1);
    }

    close(serverfd);
    return;
}

/*
 *  ======== httpTask ========
 *  Makes a HTTP GET request
 */
void recvTimeFromNTP(char *serverIP, int serverPort, char *data, int size)
{
        System_printf("NTP started\n");
        System_flush();

        int sockfd, connStat;
        struct sockaddr_in serverAddr;

        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd == -1) {
            System_printf("Socket not created");
            BIOS_exit(-1);
        }
        memset(&serverAddr, 0, sizeof(serverAddr));  // clear serverAddr structure
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(37);     // convert port # to network order
        inet_pton(AF_INET, serverIP , &(serverAddr.sin_addr));

        connStat = connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
        if(connStat < 0) {
            System_printf("sendData2Server::Error while connecting to server\n");
            if(sockfd>0) close(sockfd);
            BIOS_exit(-1);
        }

        recv(sockfd, takenTime, sizeof(takenTime), 0);
        if(recv(sockfd, takenTime, sizeof(takenTime), 0) < 0) {
            System_printf("Error while receiving data from server\n");
            if (sockfd > 0) close(sockfd);
            BIOS_exit(-1);
        }
        if (sockfd > 0) close(sockfd);
}

Void socketTask(UArg arg0, UArg arg1)
{

    Semaphore_pend(semaphore1, BIOS_WAIT_FOREVER);

    GPIO_write(Board_LED0, 1); // turn on the LED

    recvTimeFromNTP(TIMEIP, 5011,curtime, strlen(curtime));
    GPIO_write(Board_LED0, 0);  // turn off the LED

}

//  This function is called when IP Addr is added or deleted
void netIPAddrHook(unsigned int IPAddr, unsigned int IfIdx, unsigned int fAdd)
{
    static Task_Handle taskHandle1, taskHandle2, taskHandle3, taskHandle4, taskHandle5;
    Task_Params taskParams;
    Error_Block eb;

    // Create a HTTP task when the IP address is added
    if (fAdd) {

        Error_init(&eb);

        Task_Params_init(&taskParams);
        taskParams.stackSize = 4096;
        taskParams.priority = 1;
        taskHandle1 = Task_create((Task_FuncPtr)TaskFunc, &taskParams, &eb);

        Task_Params_init(&taskParams);
        taskParams.stackSize = 4096;
        taskParams.priority = 1;
        taskHandle2 = Task_create((Task_FuncPtr)clientSocketTask, &taskParams, &eb);

        Task_Params_init(&taskParams);
        taskParams.stackSize = 4096;
        taskParams.priority = 1;
        taskHandle3 = Task_create((Task_FuncPtr)serverSocketTask, &taskParams, &eb);

        Task_Params_init(&taskParams);
        taskParams.stackSize = 4096;
        taskParams.priority = 1;
        taskHandle4 = Task_create((Task_FuncPtr)taskFunc0, &taskParams, &eb);

        Task_Params_init(&taskParams);
        taskParams.stackSize = 4096;
        taskParams.priority = 1;
        taskHandle5 = Task_create((Task_FuncPtr)socketTask, &taskParams, &eb);

        if (taskHandle1 == NULL ||  taskHandle2 == NULL || taskHandle3 == NULL || taskHandle4 == NULL || taskHandle5 == NULL) {
            printError("netIPAddrHook: Failed to create HTTP, Socket and Server Tasks\n", -1);
        }

    }
}


int main(void)
{
    /* Call board init functions */
    Board_initGeneral();
    Board_initGPIO();
    Board_initEMAC();
    Board_initI2C();

    /* Turn on user LED */
    GPIO_write(Board_LED0, Board_LED_ON);

    System_printf("Starting the HTTP GET example\nSystem provider is set to "
            "SysMin. Halt the target to view any SysMin contents in ROV.\n");
    /* SysMin will only print to the console when you call flush or exit */
    System_flush();

    BIOS_start();

    return (0);
}
