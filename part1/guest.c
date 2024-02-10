#include <stddef.h>
#include <stdint.h>
// #include<stdio.h>>
// #include<string.h>
char ans[1000];
static void outb(uint16_t port, uint8_t value) {
	asm("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
}

static void out32(uint16_t port, uint32_t value) {
	asm("outl %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
}

// static void out64(uint16_t port, unsigned long int value) {
// 	asm("outl %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
// }

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ( "inb %w1, %b0"
                   : "=a"(ret)
                   : "Nd"(port)
                   : "memory");
    return ret;
}
void HC_print8bit(uint8_t val)
{
	outb(0xE9, val);
}

void HC_print32bit(uint32_t val)
{
	// Fill in here
	out32(0xE1,val);
}

uint32_t HC_numExits()
{
	// Fill in here
	uint32_t ans=inb(0xE2);
	return ans;	// Remove this
}

void HC_printStr(char *str)
{
	// Fill in here

	uint32_t ret = (uintptr_t)str;

	out32(0xE3,ret);
	// str++;		// Remove this
}

uint32_t HC_num_IN_exits()
{
	// Fill in here
	uint32_t ans=inb(0xE4);
	return ans;	// Remove this
} 

uint32_t HC_num_OUT_exits()
{
	// Fill in here
	uint32_t ans=inb(0xE5);
	return ans;	// Remove this
} 

void joinStr(char* s1,char* s2){
	int i=0;
	int j=0;
	while(s1[i]!='\0'){
		i++;
	}

	while(s2[j]!='\0'){
		s1[i++]=s2[j++];
	}
}

void reverse_string(char* str, int length) {
    int start = 0;
    int end = length - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

void int_to_string(char* s1,uint32_t num){
	int i=0;

	while(num!=0){
		s1[i++]='0'+(num%10);
		num/=10;
	}

	if(i==0){
		s1[i++]='0';
	}

	s1[i]='\0';
	reverse_string(s1,i);
}

void resetString(char* s1,int size){
	for(int i=0;i<size;i++)
		s1[i]='\0';
}
char *HC_numExitsByType()
{
	// Fill in here
	resetString(ans,1000);
	uint32_t inExits = HC_num_IN_exits();
	uint32_t outExits = HC_num_OUT_exits();
	char inCnt[10];
	char outCnt[10];
	resetString(inCnt,10);
	resetString(outCnt,10);
	int_to_string(inCnt,inExits);
	int_to_string(outCnt,outExits);
	joinStr(ans,"IO in: ");
	joinStr(ans,inCnt);
	joinStr(ans,"\n");
	joinStr(ans,"IO out: ");
	joinStr(ans,outCnt);
	joinStr(ans,"\n");
	return ans;	// Remove this
}

uint32_t HC_gvaToHva(uint32_t gva)
{
	// Fill in here
	out32(0xE6,(uintptr_t)&gva);
	return gva;	// Remove this
}

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	const char *p;
	
	// char *my_str = HC_numExitsByType();
	// HC_printStr(my_str);

	// my_str = HC_numExitsByType();
	// HC_printStr(my_str);
	for (p = "Hello 695!\n"; *p; ++p)
		HC_print8bit(*p);
	
	

	/*----------Don't modify this section. We will use grading script---------*/
	/*---Your submission will fail the testcases if you modify this section---*/
	HC_print32bit(2048);
	HC_print32bit(4294967295);

	uint32_t num_exits_a, num_exits_b;
	num_exits_a = HC_numExits();

	char *str = "CS695 Assignment 2\n";
	HC_printStr(str);

	num_exits_b = HC_numExits();

	HC_print32bit(num_exits_a);
	HC_print32bit(num_exits_b);

	char *firststr = HC_numExitsByType();
	uint32_t hva;
	hva = HC_gvaToHva(1024);
	HC_print32bit(hva);
	hva = HC_gvaToHva(4294967295);
	HC_print32bit(hva);
	char *secondstr = HC_numExitsByType();

	HC_printStr(firststr);
	HC_printStr(secondstr);
	/*------------------------------------------------------------------------*/

	*(long *) 0x400 = 42;

	for (;;)
		asm("hlt" : /* empty */ : "a" (42) : "memory");
}
