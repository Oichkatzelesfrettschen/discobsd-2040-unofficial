/* A linked list built with the tree's malloc and torn down with free. */
int printf(char *fmt, ...);
void *malloc(unsigned);
void free(void *);

struct node {
	int value;
	struct node *next;
};

int
main(void)
{
	struct node *head;
	struct node *n;
	struct node *next;
	int i;
	int sum;
	int count;

	head = 0;
	for (i = 1; i <= 10; i++) {
		n = malloc(sizeof(struct node));
		if (n == 0) {
			printf("malloc failed\n");
			return 1;
		}
		n->value = i * i;
		n->next = head;
		head = n;
	}

	sum = 0;
	count = 0;
	for (n = head; n != 0; n = n->next) {
		sum = sum + n->value;
		count = count + 1;
	}
	printf("%d %d\n", count, sum);

	printf("%d %d %d\n", head->value, head->next->value,
	    head->next->next->value);

	n = head;
	while (n != 0) {
		next = n->next;
		free(n);
		n = next;
	}
	printf("freed\n");
	return 0;
}
