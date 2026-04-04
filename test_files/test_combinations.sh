#!/bin/bash
echo "=== Testing Composed Compound Combinations ==="
echo ""

# Test 1
echo "Test 1: command < input.txt"
./myshell << 'SHELL'
cat < input.txt
exit
SHELL
echo ""

# Test 2
echo "Test 2: command > output.txt"
./myshell << 'SHELL'
echo "Test output" > output.txt
cat output.txt
exit
SHELL
echo ""

# Test 3
echo "Test 3: command 2> error.log"
./myshell << 'SHELL'
ls /nonexistent 2> error.log
cat error.log
exit
SHELL
echo ""

# Test 4
echo "Test 4: command1 < input.txt | command2"
./myshell << 'SHELL'
cat < input.txt | wc -l
exit
SHELL
echo ""

# Test 5
echo "Test 5: command1 | command2 > output.txt"
./myshell << 'SHELL'
echo "piped output" | cat > output.txt
cat output.txt
exit
SHELL
echo ""

# Test 6
echo "Test 6: command1 | command2 2> error.log"
./myshell << 'SHELL'
echo "test" | grep nonexistent 2> error.log
cat error.log
exit
SHELL
echo ""

# Test 7
echo "Test 7: command < input.txt > output.txt"
./myshell << 'SHELL'
cat < input.txt > output.txt
cat output.txt
exit
SHELL
echo ""

# Test 8
echo "Test 8: command1 < input.txt | command2 > output.txt"
./myshell << 'SHELL'
cat < input.txt | sort > output.txt
cat output.txt
exit
SHELL
echo ""

# Test 9
echo "Test 9: command1 < input.txt | command2 | command3 > output.txt"
./myshell << 'SHELL'
cat < input.txt | sort | uniq > output.txt
cat output.txt
exit
SHELL
echo ""

# Test 10
echo "Test 10: command1 | command2 | command3 2> error.log"
./myshell << 'SHELL'
echo "test" | grep test | cat 2> error.log
exit
SHELL
echo ""

# Test 11
echo "Test 11: command1 < input.txt | command2 2> error.log | command3 > output.txt"
./myshell << 'SHELL'
cat < input.txt | grep z 2> error.log | wc -l > output.txt
cat output.txt
exit
SHELL

echo ""
echo "=== All tests completed ==="
