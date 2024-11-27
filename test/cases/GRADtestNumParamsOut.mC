int func1(int a, int b) {
  output(1);
  return 1;
}

int func2(int x, int y, int z) {
  output(2);
  return 2;
}

void main() {
  func1(27, 42);
  func2(108, 789, 56);
}
