typedef struct io_interface_node{
	io_interface current;
	struct io_interface_node *next;
} io_interface_node;

typedef struct {
	io_interface_node *head;
	io_interface_node *tail;
} io_interface_queue;




int enqueue(io_interface_queue *queue, io_interface_node *node) {

	if (queue->head == NULL) {
		queue->head = node;
		queue->tail = node;
		queue->head->next = NULL;

	}
	else {
		queue->tail->next = node;
		queue->tail = node;
		queue->tail->next = NULL;
	}

	return 0;
}

io_interface_node *dequeue(io_interface_queue *queue) {

	if (queue->head != NULL) {

		io_interface_node *to_ret = queue->head;
		queue->head = queue->head->next;

		return to_ret;
	}
	else {
		return NULL;
	}
}