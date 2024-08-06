#include <stdio.h>
#include <stdint.h>

void output_count(int count)
{
  while (1) {
    int amt = (count >= 255 ? 255 : count);
    printf("%d,", amt);
    count -= amt;
    if (count == 0) break;
    printf("0,");
  }
}

int main()
{
  int last = 0;
  int count = 0;
  int c;
  while ((c = getchar()) != EOF) {
    if (c != last) {
      output_count(count);
      last = c;
      count = 0;
    }
    count++;
  }
  output_count(count);

  return 0;
}
