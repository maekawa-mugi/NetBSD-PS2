#include <stdio.h>
#include <string.h>
int main(void)
{
	volatile double a = 1.5, b = 2.25;
	volatile long double x = a, y = b, z;
	char buf[64];
	unsigned char *p;
	int i;
	printf("sizeof(long double)=%d\n", (int)sizeof(long double));
	p = (unsigned char *)&x;
	printf("x bytes: ");
	for (i = 15; i >= 0; i--) printf("%02x", p[i]);
	printf("\n");
	{
		volatile float fa = 1.5f;
		volatile long double fx = fa;
		volatile int ia = 3;
		volatile long double ix = ia;
		volatile long long la = 3;
		volatile long double lx = la;
		volatile double back;
		p = (unsigned char *)&fx; printf("float->ld: ");
		for (i = 15; i >= 0; i--) printf("%02x", p[i]);
		p = (unsigned char *)&ix; printf("\nint->ld:   ");
		for (i = 15; i >= 0; i--) printf("%02x", p[i]);
		p = (unsigned char *)&lx; printf("\nllong->ld: ");
		for (i = 15; i >= 0; i--) printf("%02x", p[i]);
		back = (double)x;
		printf("\nback=%f\n", (double)back);
	}
	z = x * y;
	printf("x==1.5: %d y==2.25: %d\n", x == 1.5L, y == 2.25L);
	printf("z==3.375L: %d  (double)z==3.375: %d\n", z == 3.375L, (double)z == 3.375);
	p = (unsigned char *)&z;
	for (i = 15; i >= 0; i--) printf("%02x", p[i]);
	printf("\n");
	z = 3.375L;
	p = (unsigned char *)&z;
	for (i = 15; i >= 0; i--) printf("%02x", p[i]);
	printf("\n");
	z = x + y; printf("add==3.75: %d\n", z == 3.75L);
	z = y / x; printf("div==1.5: %d\n", z == 1.5L);
	snprintf(buf, sizeof(buf), "%Lf", x * y);
	printf("%s\n", buf);
	return 0;
}
