#ifndef I2C_H_
#define I2C_H_

bool IIC_writeReg(int device_ID, int addr, uint8_t val);
bool IIC_readReg(int device_ID, int addr, int no_of_bytes, char *buf);

#endif /* I2C_H_ */
