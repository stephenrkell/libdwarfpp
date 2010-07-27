#include <stdio.h>

struct foo 
{
	int a;
	float b;
};

//struct bar
//{
	
	

int main()
{
	struct foo f;
	f.a = 42;
	f.b = 3.141;
	printf("Hello, world! a is %d, b is %f.\n", f.a, f.b);
	return 0;
}
