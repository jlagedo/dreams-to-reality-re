/* Segment selectors are 16 bits; default PUSH/POP stack slots in Win32 are
 * nevertheless 32 bits. The upper stored selector half is not inspected. */
#include <stdio.h>
unsigned push_es_size(void);
#pragma aux push_es_size = \
    "mov edx,esp" "push es" "mov eax,edx" "sub eax,esp" "pop es" \
    value [eax] modify exact [eax edx];
unsigned push_ds_size(void);
#pragma aux push_ds_size = \
    "mov edx,esp" "push ds" "mov eax,edx" "sub eax,esp" "pop ds" \
    value [eax] modify exact [eax edx];
unsigned push_es_word_size(void);
#pragma aux push_es_word_size = \
    "mov edx,esp" "db 66h,06h" "mov eax,edx" "sub eax,esp" "db 66h,07h" \
    value [eax] modify exact [eax edx];
unsigned return_marker(void);
#pragma aux return_marker = \
    "push 11223344h" "push es" "mov eax,[esp+4]" "pop es" "add esp,4" \
    value [eax] modify exact [eax];
int main(void) {
    printf("push-es %u\n",push_es_size());
    printf("push-ds %u\n",push_ds_size());
    printf("push-es-word %u\n",push_es_word_size());
    printf("return-marker %08x\n",return_marker());
    return 0;
}
