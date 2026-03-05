#include "../include/string.h"
// Assuming common types (like size_t) are available globally via included headers.

/**
 * Fills a block of memory with a specified value.
 *
 * @param s The starting address of the memory block to fill.
 * @param c The value to be set (passed as an int, but converted to unsigned char).
 * @param n The number of bytes to be set to the value.
 * @return The pointer to the memory area s.
 */
void* memset(void* s, int c, size_t n) {
	unsigned char* p = (unsigned char*)s;
	unsigned char val = (unsigned char)c;
	for (size_t i = 0; i < n; i++) {
		p[i] = val;
	}
	return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
	unsigned char* d = (unsigned char*)dest;
	const unsigned char* s = (const unsigned char*)src;

	// Copies byte by byte
	for (size_t i = 0; i < n; i++) {
		d[i] = s[i];
	}

	return dest;
}

void strcat(char* dest, const char* source){
	int i = 0, j = 0;
	while(dest[i] != '\0'){
		i++;
	}
	while(source[j] != '\0'){
		dest[i] = source[j];
		i++;
		j++;
	}
	dest[i] = '\0';
}

/**
 * Compares two strings.
 * @param str1 The first string.
 * @param str2 The second string.
 * @return An integer less than, equal to, or greater than zero if str1 is found,
 * respectively, to be less than, to match, or be greater than str2.
 */
int strcmp(const char* str1, const char* str2) {
	while (*str1 && (*str1 == *str2)) {
		str1++;
		str2++;
	}
	return *(unsigned char*)str1 - *(unsigned char*)str2;
}

/**
 * Copies the string pointed to by src to dest.
 * DANGER: Does not check buffer bounds.
 * @param dest Destination buffer.
 * @param src Source string.
 */
void strcpy(char* dest, const char* src) {
	while (*src) {
		*dest++ = *src++;
	}
	*dest = '\0';
}

/**
 * Copies up to n characters from the string pointed to by src to dest.
 * If the length of src is less than n, the remainder of dest is padded with null bytes.
 * @param dest Destination buffer.
 * @param src Source string.
 * @param n Maximum number of characters to copy.
 * @return The destination pointer.
 */
char* strncpy(char* dest, const char* src, size_t n) {
	size_t i;
	for (i = 0; i < n && src[i] != '\0'; i++) {
		dest[i] = src[i];
	}
	// Pad the remaining part of dest with null bytes if src is shorter than n
	for (; i < n; i++) {
		dest[i] = '\0';
	}
	return dest;
}

/**
 * Calculates the length of a string.
 * @param str The string.
 * @return The length of the string.
 */
int strlen(const char* str) {
	int len = 0;
	while (str[len]) len++;
	return len;
}

/**
 * Locates the first occurrence of the character c (converted to a char) in the string s.
 * The terminating null character is considered to be part of the string.
 *
 * @param s The string to be searched.
 * @param c The character to search for.
 * @return A pointer to the first occurrence of the character c in the string s, or NULL if the character is not found.
 */
char* strchr(const char* s, int c) {
	// Cast c to unsigned char for comparison, as required by standard strchr.
	unsigned char ch = (unsigned char)c;

	// Iterate through the string, including the null terminator
	while (1) {
		if (*s == ch) {
			// Found a match (or the null terminator if c was 0)
			return (char*)s;
		}

		if (*s == '\0') {
			// Reached the end without finding the character (and c was not 0)
			return (char*)0; // Return NULL
		}

		s++;
	}
}

/**
 * Locates the last occurrence of the character c (converted to a char) in the string s.
 */
char* strrchr(const char* s, int c) {
    unsigned char ch = (unsigned char)c;
    char* last = (char*)0;
    
    while (1) {
        if (*s == ch) {
            last = (char*)s;
        }
        if (*s == '\0') {
            break;
        }
        s++;
    }
    
    return last;
}

/**
 * Converts an integer to a string.
 */
void int_to_str(int num, char* str) {
	int i = 0;
	int is_negative = 0;

	if (num < 0) {
		is_negative = 1;
		num = -num;
	}

	if (num == 0) {
		str[i++] = '0';
	} else {
		char temp[12]; // Max 11 digits + sign
		int j = 0;

		while (num > 0) {
			temp[j++] = (num % 10) + '0';
			num /= 10;
		}

		if (is_negative) {
			str[i++] = '-';
		}

		while (j > 0) {
			str[i++] = temp[--j];
		}
	}

	str[i] = '\0';
}

/**
 * Converts an integer to hexadecimal string
 */
void int_to_hex(uint32_t num, char* str) {
	const char hex_chars[] = "0123456789ABCDEF";
	int i = 0;
	
	if (num == 0) {
		str[i++] = '0';
	} else {
		char temp[9]; // Max 8 hex digits + null
		int j = 0;
		
		while (num > 0) {
			temp[j++] = hex_chars[num % 16];
			num /= 16;
		}
		
		while (j > 0) {
			str[i++] = temp[--j];
		}
	}
	
	str[i] = '\0';
}

/**
 * Converts a string to an integer.
 */
int str_to_int(const char* str) {
	int result = 0;
	int sign = 1;
	int i = 0;

	if (str[0] == '-') {
		sign = -1;
		i = 1;
	}

	while (str[i] >= '0' && str[i] <= '9') {
		result = result * 10 + (str[i] - '0');
		i++;
	}

	return result * sign;
}
