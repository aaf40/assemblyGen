import sys
import difflib

def compare_files(expected_file, actual_file):
    try:
        with open(expected_file, 'r') as f1, open(actual_file, 'r') as f2:
            expected = f1.read().strip()
            actual = f2.read().strip()
            
            test_name = expected_file.split('/')[-1]
            
            # Compare content without empty lines and DEBUG lines
            expected_content = [line.strip() for line in expected.splitlines() 
                              if line.strip() and not line.strip().startswith("DEBUG:")]
            actual_content = [line.strip() for line in actual.splitlines() 
                            if line.strip() and not line.strip().startswith("DEBUG:")]
            
            if expected_content == actual_content:
                if expected.splitlines(keepends=True) == actual.splitlines(keepends=True):
                    print("\nOutput is exactly the same in both files (excluding DEBUG lines):")
                    print("----------------------------------------------------------")
                    print("\n".join(expected_content))
                    print(f"\033[92m{test_name} passed ✓\033[0m")  # Green
                else:
                    print("\nContent matches but newlines differ (excluding DEBUG lines):")
                    print("----------------------------------------------------")
                    print("\ncurrent output:")
                    print("---------------")
                    print("\n".join(actual_content))
                    print("\nexpected output:")
                    print("----------------")
                    print("\n".join(expected_content))
                    print(f"\033[93m{test_name} passed (with newline differences) ✓\033[0m")  # Yellow
                return
            
            # Filter out DEBUG lines for display
            expected = "\n".join(line for line in expected.splitlines() 
                               if not line.strip().startswith("DEBUG:"))
            actual = "\n".join(line for line in actual.splitlines() 
                             if not line.strip().startswith("DEBUG:"))
            
            # Print the outputs first
            print("\ncurrent output:")
            print("---------------")
            print(actual)
            print("\nexpected output:")
            print("----------------")
            print(expected)
            
            # Generate and print the differences
            print("\nDifferences found:")
            print("------------------")
            expected_lines = expected.splitlines()
            actual_lines = actual.splitlines()
            
            differ = difflib.Differ()
            diff = list(differ.compare(expected_lines, actual_lines))
            
            prev_line = None
            prev_line_num = None
            actual_line_num = 0
            expected_line_num = 0
            
            for line in diff:
                if line.startswith('  '):  # Line is same in both
                    actual_line_num += 1
                    expected_line_num += 1
                    continue
                elif line.startswith('- '):  # Line only in expected
                    expected_line_num += 1
                    content = line[2:].strip()
                    if content:  # Only process non-empty lines
                        prev_line = content
                        prev_line_num = expected_line_num
                elif line.startswith('+ '):  # Line only in actual
                    actual_line_num += 1
                    content = line[2:].strip()
                    if content:  # Only process non-empty lines
                        if prev_line:
                            print(f"\033[91mFound    (line {actual_line_num}): {content}")
                            print(f"Expected (line {prev_line_num}): {prev_line}\033[0m")
                            prev_line = None
                        else:
                            print(f"\033[91mExtra line {actual_line_num}: {content}\033[0m")
                elif line.startswith('? '):  # Indicates where strings differ
                    continue
            
            # Print any remaining expected lines that had no matching actual lines
            if prev_line:
                print(f"\033[91mMissing line {prev_line_num}: {prev_line}\033[0m")
            
            print(f"\033[91m{test_name} failed ✗\033[0m")  # Red
                    
    except FileNotFoundError as e:
        print(f"ERROR: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python compare.py <expected_file> <actual_file>")
        sys.exit(1)
        
    compare_files(sys.argv[1], sys.argv[2])
