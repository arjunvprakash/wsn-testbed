#define GPIO_LOW 0
#define GPIO_HIGH 1
#define GPIO_IN 0
#define GPIO_OUT 1

void GPIO_init();
void GPIO_setup(int, int);
int GPIO_input(int);
void GPIO_output(int, int);
