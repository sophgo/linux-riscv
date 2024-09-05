#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "argvtoint.h"

#define true  1
#define false 0

enum Status { kValid = 0, kInvalid };
int g_nStatus = kValid;

int StrToInt(const char *str)
{
	g_nStatus = kInvalid;
	long long num = 0;

	if (str != NULL && *str != '\0') {
		unsigned char minus = false;

		if (*str == '+')
			str++;
		else if (*str == '-') {
			str++;
			minus = true;
		}

		if (*str != '\0')
			num = StrToIntCore(str, minus);
	}

	return (int) num;
}

long long StrToIntCore(const char *digit, unsigned char minus)
{
	long long num = 0;

	while (*digit != '\0') {
		if (*digit >= '0' && *digit <= '9') {
			int flag = minus ? -1 : 1;

			num = num * 10 + flag * (*digit - '0');

			if ((!minus && num > 0x7FFFFFFF)
				|| (minus && num <  0x80000000)) {
				num = 0;
				break;
			}

			digit++;
		} else {
			num = 0;
			break;
		}
	}

	if (*digit == '\0')
		g_nStatus = kValid;

	return num;
}

int CharToInt(char s[])
{
	int i, n;

	for (i = 0, n = 0; isdigit(s[i]); i++)
		n = 10 * n + (s[i] - '0');	//将数字字符转换成整形数字

	return n;
}

int parser_str(char *s)
{
	int i, m, temp = 0, n;
	const char ch = 'x';
	char *pnew;

	pnew = strrchr(s, ch) + 1;
	if (pnew == NULL)
		printf("input error number\n");

	m = strlen(s) - 2;			//get string length
	//printf("str len = %d", m);

	for (i = 0; i < m; i++) {
		if (*(pnew + i) >= 'A' && *(pnew + i) <= 'F')
			n = *(pnew + i) - 'A' + 10;
		else if (*(pnew + i) >= 'a' && *(pnew + i) <= 'f')
			n = *(pnew + i) - 'a' + 10;
		else
			n = *(pnew + i) - '0';

		temp = temp * 16 + n;
	}

	return temp;
}

char *IntToHexStr(uint32_t wdata, char *buffer)
{
	static int i;

	if (wdata < 16) {
		if (wdata < 10)
			buffer[i] = wdata + '0';
		else
			buffer[i] = wdata - 10 + 'a';

		buffer[i + 1] = '\0';
	} else {
		IntToHexStr(wdata / 16, buffer);
		i++;
		wdata %= 16;
		if (wdata < 10)
			buffer[i] = wdata + '0';
		else
			buffer[i] = wdata - 10 + 'a';
	}

	return buffer;
}
