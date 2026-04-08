#include <print.h>

/* forward declaration */
static void print_char(fmt_callback_t, void *, char, int, int);
static void print_str(fmt_callback_t, void *, const char *, int, int);
static void print_num(fmt_callback_t, void *, unsigned long, int, int, int, int, char, int);

void vprintfmt(fmt_callback_t out, void *data, const char *fmt, va_list ap) {
	char c;
	const char *s;
	long num;

	int width;
	int long_flag; // output is long (rather than int)
	int neg_flag;  // output is negative
	int ladjust;   // output is left-aligned
	char padc;     // padding char

	for (;;) {
		/* scan for the next '%' */
		/* Exercise 1.4: Your code here. (1/8) */
		const char *temp = fmt;
		while(*fmt != '\0' && *fmt != '%') {
			fmt++;
		}

		/* flush the string found so far */
		/* Exercise 1.4: Your code here. (2/8) */
		if (fmt > temp) {
			out(data, temp, fmt-temp);
		}

		/* check "are we hitting the end?" */
		/* Exercise 1.4: Your code here. (3/8) */
		if(*fmt == '\0') {
			break;
		}

		/* we found a '%' */
		/* Exercise 1.4: Your code here. (4/8) */
		if(*fmt == '%') {
			fmt++;
		}
		

		/* check format flag */
		/* Exercise 1.4: Your code here. (5/8) */
		ladjust = 0;
		padc = ' ';
		while (*fmt == '-' || *fmt == '0') {
			if(*fmt == '-') {
				ladjust = 1;
			}
			else {
				padc = '0';
			}
			fmt++;
		}

		/* get width */
		/* Exercise 1.4: Your code here. (6/8) */
		width = 0;
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt - '0');
			fmt++;
		}

		/* check for long */
		/* Exercise 1.4: Your code here. (7/8) */
		long_flag = 0;
		if(*fmt == 'l') {
			long_flag = 1;
			fmt++;
		}

		neg_flag = 0;
		switch (*fmt) {
		case 'b':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}
			print_num(out, data, num, 2, 0, width, ladjust, padc, 0);
			break;

		case 'd':
		case 'D':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}

			/*
			 * Refer to other parts (case 'b', case 'o', etc.) and func 'print_num' to
			 * complete this part. Think the differences between case 'd' and the
			 * others. (hint: 'neg_flag').
			 */
			/* Exercise 1.4: Your code here. (8/8) */
			if(num < 0) {
				neg_flag = 1;
				num = -num;
			}
			print_num(out, data, num, 10, neg_flag, width, ladjust, padc, 0);

			break;

		case 'o':
		case 'O':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}
			print_num(out, data, num, 8, 0, width, ladjust, padc, 0);
			break;

		case 'u':
		case 'U':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}
			print_num(out, data, num, 10, 0, width, ladjust, padc, 0);
			break;

		case 'x':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}
			print_num(out, data, num, 16, 0, width, ladjust, padc, 0);
			break;

		case 'X':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}
			print_num(out, data, num, 16, 0, width, ladjust, padc, 1);
			break;

		case 'c':
			c = (char)va_arg(ap, int);
			print_char(out, data, c, width, ladjust);
			break;

		case 's':
			s = (char *)va_arg(ap, char *);
			print_str(out, data, s, width, ladjust);
			break;

		case '\0':
			fmt--;
			break;

		default:
			/* output this char as it is */
			out(data, fmt, 1);
		}
		fmt++;
	}
}

/* --------------- local help functions --------------------- */
void print_char(fmt_callback_t out, void *data, char c, int length, int ladjust) {
	int i;

	if (length < 1) {
		length = 1;
	}
	const char space = ' ';
	if (ladjust) {
		out(data, &c, 1);
		for (i = 1; i < length; i++) {
			out(data, &space, 1);
		}
	} else {
		for (i = 0; i < length - 1; i++) {
			out(data, &space, 1);
		}
		out(data, &c, 1);
	}
}

void print_str(fmt_callback_t out, void *data, const char *s, int length, int ladjust) {
	int i;
	int len = 0;
	const char *s1 = s;
	while (*s1++) {
		len++;
	}
	if (length < len) {
		length = len;
	}

	if (ladjust) {
		out(data, s, len);
		for (i = len; i < length; i++) {
			out(data, " ", 1);
		}
	} else {
		for (i = 0; i < length - len; i++) {
			out(data, " ", 1);
		}
		out(data, s, len);
	}
}

void print_num(fmt_callback_t out, void *data, unsigned long u, int base, int neg_flag, int length,
	       int ladjust, char padc, int upcase) {
	/* algorithm :
	 *  1. prints the number from left to right in reverse form.
	 *  2. fill the remaining spaces with padc if length is longer than
	 *     the actual length
	 *     TRICKY : if left adjusted, no "0" padding.
	 *		    if negtive, insert  "0" padding between "0" and number.
	 *  3. if (!ladjust) we reverse the whole string including paddings
	 *  4. otherwise we only reverse the actual string representing the num.
	 */

	int actualLength = 0;
	char buf[length + 70];
	char *p = buf;
	int i;

	do {
		int tmp = u % base;
		if (tmp <= 9) {
			*p++ = '0' + tmp;
		} else if (upcase) {
			*p++ = 'A' + tmp - 10;
		} else {
			*p++ = 'a' + tmp - 10;
		}
		u /= base;
	} while (u != 0);

	if (neg_flag) {
		*p++ = '-';
	}

	/* figure out actual length and adjust the maximum length */
	actualLength = p - buf;
	if (length < actualLength) {
		length = actualLength;
	}

	/* add padding */
	if (ladjust) {
		padc = ' ';
	}
	if (neg_flag && !ladjust && (padc == '0')) {
		for (i = actualLength - 1; i < length - 1; i++) {
			buf[i] = padc;
		}
		buf[length - 1] = '-';
	} else {
		for (i = actualLength; i < length; i++) {
			buf[i] = padc;
		}
	}

	/* prepare to reverse the string */
	int begin = 0;
	int end;
	if (ladjust) {
		end = actualLength - 1;
	} else {
		end = length - 1;
	}

	/* adjust the string pointer */
	while (end > begin) {
		char tmp = buf[begin];
		buf[begin] = buf[end];
		buf[end] = tmp;
		begin++;
		end--;
	}

	out(data, buf, length);
}






// lib/print.c
#include <stdarg.h>

// ==========================================
// 辅助函数：确保缓冲区里有字符
// ==========================================
static inline void ensure_char(scan_callback_t in, void *data, char *ch, int *ch_valid) {
    if (!*ch_valid) {
        in(data, ch, 1);
        *ch_valid = 1;
    }
}

// ==========================================
// 辅助函数：跳过前导空白符
// ==========================================
static inline void skip_whitespace(scan_callback_t in, void *data, char *ch, int *ch_valid) {
    ensure_char(in, data, ch, ch_valid);
    while (*ch == ' ' || *ch == '\t' || *ch == '\n' || *ch == '\r') {
        // 直接覆盖当前字符，不需要改 ch_valid 状态，因为它一直握着有效的最新字符
        in(data, ch, 1);
    }
}

// ==========================================
// 处理 %c：读第一个输入字符，无论是不是空白
// ==========================================
void scan_c(scan_callback_t in, void *data, char *ch, int *ch_valid, char *cp) {
    /* ---------------------------------- */
    /* Lab 1-extra: Your code here. (2/4)  */
    /* 提示：
       1. 使用辅助函数确保缓冲区里有字符（注意：%c 绝不能跳过空白符！）
       2. 将读取到的字符存入 cp 指向的内存。
       3. 核心：因为该字符已被 %c 实体吃掉，必须将 缓存标志位 置为无效(值为0)。
    */
    /* ---------------------------------- */
	ensure_char(in, data, ch, ch_valid);
}

// ==========================================
// 处理 %s：跳过前导空白，读到空白符终止
// ==========================================
void scan_s(scan_callback_t in, void *data, char *ch, int *ch_valid, char *cp) {
    /* ---------------------------------- */
    /* Lab 1-extra: Your code here. (3/4)  */
    /* 提示：
       1. 使用辅助函数跳过所有的前导空白符。
       2. 连续读取非空白字符并存入 cp(可以在循环内部使用 *cp++ = *ch 进行赋值)。
       3. 遇到空白符或 '\0' 时停止读取。
       4. 别忘了在字符串末尾添加 '\0' 封口。
       5. 停下时：ch 里必须正好握着导致停止的“空白符”，且保持 缓存标志位 置为有效(值为1)。
    */
    /*--------------------------------- */
	skip_whitespace(in, data, ch, ch_valid);
}

// ==========================================
// 处理 %u：跳过前导空白，读到非数字终止
// ==========================================
void scan_u(scan_callback_t in, void *data, char *ch, int *ch_valid, int *ip) {
    /* ---------------------------------- */
    /* Lab 1-extra: Your code here. (4/4)  */
    /* 提示：
       1. 定义变量用于累加数字计算结果。
       2. 使用辅助函数跳过所有的前导空白符。
       3. 兼容可选的前导 '+' 号（至多1个）（如果有，调用 in() 越过它）。
       4. 如果遇到的第一个非空白字符就不是数字或 `+`，直接赋值为 `0` 并结束。
       5. 连续读取数字字符（'0'-'9'），并 计算 十进制数值。
       6. 遇到非数字字符时停止读取，并将结果存入 ip。
       7. 停下时：ch 里必须正好握着导致停止的“非数字字符”，且保持 缓存标志位 置为有效(值为1)。
    */
    /* ---------------------------------- */

}

// ==========================================
// 主入口：vscanfmt
// ==========================================
int vscanfmt(scan_callback_t in, void *data, const char *fmt, va_list ap) {
    char ch;
    int ch_valid = 0; // 缓存标志位
    int ret = 0;

    while (*fmt) {
        if (*fmt == '%') {
            fmt++; // 跳过 '%'

            switch (*fmt) {
            case 's':
                scan_s(in, data, &ch, &ch_valid, va_arg(ap, char *));
                ret++;
                break;

            case 'u':
                scan_u(in, data, &ch, &ch_valid, va_arg(ap, int *));
                ret++;
                break;

            case 'c':
                scan_c(in, data, &ch, &ch_valid, va_arg(ap, char *));
                ret++;
                break;


            }
        }
        fmt++;
    }
    return ret;
}
