int main(void)
{
	struct Local {
		struct Inner {
			struct Local *owner;
		} inner;
	} temp;
	
	return 0;
}
