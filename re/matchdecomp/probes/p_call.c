extern int h(int, int, int);
int f_call(int a, int b) { return h(b, a, a + b) + 1; }
