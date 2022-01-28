#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/I2C.h>

#include "Board.h"
#include "I2C.h"

I2C_Handle      i2c;
I2C_Params      i2cParams;
I2C_Transaction i2cTransaction;

bool IIC_writeReg(int device_ID, int addr, uint8_t val)
{
    uint8_t txBuffer[2];            //transmitter,receiver
    uint8_t rxBuffer[2];
    bool flag=false;

    // place parameters
    txBuffer[0] = addr;                             // register address
    txBuffer[1] = val;                              // value to be written
    i2cTransaction.slaveAddress = device_ID;        // device IIC ID
    i2cTransaction.writeBuf = txBuffer;             // buffer that holds the values to be written
    i2cTransaction.writeCount = 2;                  // 2 bytes will be sent
    i2cTransaction.readBuf = rxBuffer;              // receive buffer (in this case it is not used)
    i2cTransaction.readCount = 0;                   // no bytes to be received

    if (I2C_transfer(i2c, &i2cTransaction)) {       // start the transaction
        flag = true;                              // true will be returned to indicate that transaction is successful
    }
    else {
        System_printf("I2C Bus fault\n");           // there is an error, returns false
    }
    System_flush();

    return flag;
}

bool IIC_readReg(int device_ID, int addr, int no_of_bytes, char *buf)
{
    uint8_t txBuffer[2];
    bool flag=false;

    txBuffer[0] = addr;                             // 1 byte: register address
    i2cTransaction.slaveAddress = device_ID;        // Device Id
    i2cTransaction.writeBuf = txBuffer;             // buffer to be sent
    i2cTransaction.writeCount = 1;                  // send just register address
    i2cTransaction.readBuf = buf;                   // read into this buffer
    i2cTransaction.readCount = no_of_bytes;         // number of bytes to be read


    if (I2C_transfer(i2c, &i2cTransaction)) {
        flag=true;
    }
    else {
        System_printf("I2C Bus fault\n");
    }
    System_flush();

    return flag;
}

