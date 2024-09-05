#ifndef _ARGVTOINT_H_
#define _ARGVTOINT_H_

typedef unsigned int            uint32_t;

int StrToInt(const char *str);
int CharToInt(char s[]);
int parser_str(char *s);
char *IntToHexStr(uint32_t wdata, char *buffer);
long long StrToIntCore(const char *digit, unsigned char minus);


#endif
