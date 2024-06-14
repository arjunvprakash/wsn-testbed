#ifndef I2C_H
#define I2C_H

int i2c_open(int);
int i2c_close(int);
char i2c_read(int, char);
int i2c_read_block(int, char, char*, int);
int i2c_write(int, char, char);

#endif // I2C_H
