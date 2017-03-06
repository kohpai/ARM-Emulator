#define STACK_SIZE (100 * 1024)
char __stack[STACK_SIZE];

unsigned unicorn_uart_rx(void) __attribute__((noinline));
unsigned unicorn_uart_rx(void)
{
    while (1);
    return 0;
}

void unicorn_uart_tx(char) __attribute__((noinline));
void unicorn_uart_tx(char c)
{
    while (1);
}

static void print(char *p)
{
    while (*p) {
        unicorn_uart_tx(*p++);
    }
}

int main(int argc, char *argv[])
{
    /* unsigned char exiting = 0; */
    print("Hello World!\n");
    print("Hello Kohpai!\n");
    print("Hello Ox!\n");
    print("Hello George!\n");

    /* while (!exiting) { */
    /*     unsigned c = unicorn_uart_rx(); */
    /*     switch (c) { */
    /*         case 'q':  */
    /*             print("\nQuit!\n");  */
    /*             exiting = 1; */
    /*             break; */
    /*         default : unicorn_uart_tx((char)c); break; */
    /*     }; */
    /* } */

    return 0;
} 

void __attribute__((noreturn)) _exit(int e)
{
	while (1);
}
