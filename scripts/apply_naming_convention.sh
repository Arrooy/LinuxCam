#!/bin/bash

# Script to apply systematic naming convention fixes using clang-tidy
# Focuses specifically on readability-identifier-naming violations
# Usage: ./apply_naming_convention.sh [file_or_directory]

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
COMPILE_COMMANDS="${BUILD_DIR}/compile_commands.json"

# Ensure we have compile_commands.json
if [[ ! -f "$COMPILE_COMMANDS" ]]; then
    echo -e "${RED}Error: compile_commands.json not found at $COMPILE_COMMANDS${NC}"
    echo "Please run cmake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON first."
    exit 1
fi

# Function to show usage
show_usage() {
    echo "Usage: $0 [file_or_directory]"
    echo ""
    echo "This script applies systematic naming convention fixes using clang-tidy."
    echo "It focuses on readability-identifier-naming violations."
    echo ""
    echo "Examples:"
    echo "  $0 src/main.cpp                   # Apply naming fixes to single file"
    echo "  $0 include/LinuxFace/             # Apply naming fixes to directory"
    echo "  $0                                # Apply naming fixes to all source files"
}

# Function to get all source files
get_source_files() {
    find "$PROJECT_ROOT" \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) \
        -not -path "*/external/*" \
        -not -path "*/build/*" \
        -not -path "*/.git/*" | \
        sort
}

# Function to run clang-tidy with naming focus on a single file
run_naming_fixes() {
    local file="$1"
    local relative_file="${file#$PROJECT_ROOT/}"
    
    echo -e "${BLUE}Applying naming fixes to: $relative_file${NC}"
    
    # Run clang-tidy with only naming-related checks
    clang-tidy \
        --checks='-*,readability-identifier-naming' \
        --config-file="$PROJECT_ROOT/.clang-tidy" \
        -p "$BUILD_DIR" \
        --fix \
        --fix-errors \
        "$file" \
        2>&1 | sed "s|$PROJECT_ROOT/||g"
    
    local exit_code=${PIPESTATUS[0]}
    if [[ $exit_code -eq 0 ]]; then
        echo -e "${GREEN}✓ Completed naming fixes: $relative_file${NC}"
    else
        echo -e "${YELLOW}⚠ Issues found in: $relative_file${NC}"
    fi
    
    return $exit_code
}

# Function to check for naming violations before fixing
check_naming_violations() {
    local file="$1"
    local relative_file="${file#$PROJECT_ROOT/}"
    
    echo -e "${BLUE}Checking naming violations in: $relative_file${NC}"
    
    # Check for naming violations
    local output
    output=$(clang-tidy \
        --checks='-*,readability-identifier-naming' \
        --config-file="$PROJECT_ROOT/.clang-tidy" \
        -p "$BUILD_DIR" \
        "$file" \
        2>&1 | sed "s|$PROJECT_ROOT/||g")
    
    if echo "$output" | grep -q "warning:\|error:"; then
        echo "$output"
        return 1
    else
        echo -e "${GREEN}✓ No naming violations found${NC}"
        return 0
    fi
}

# Main logic
main() {
    cd "$PROJECT_ROOT"
    
    case "${1:-}" in
        --help)
            show_usage
            exit 0
            ;;
        "")
            # Apply naming fixes to all files
            echo -e "${YELLOW}Applying naming convention fixes to all source files...${NC}"
            echo -e "${YELLOW}This will modify files in place. Make sure you have backups!${NC}"
            echo ""
            echo "The following naming conventions will be enforced:"
            echo "  - Functions: camelCase"
            echo "  - Variables: camelCase"
            echo "  - Classes: CamelCase"
            echo "  - Members: lower_case (with _ suffix for private)"
            echo "  - Constants: UPPER_CASE"
            echo "  - Namespaces: lower_case"
            echo ""
            read -p "Continue? (y/N): " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                echo "Aborted."
                exit 0
            fi
            
            local processed=0
            local failed=0
            local fixed=0
            
            while IFS= read -r file; do
                if [[ -f "$file" ]]; then
                    ((processed++))
                    
                    # First check if file has violations
                    if check_naming_violations "$file" >/dev/null 2>&1; then
                        echo -e "${GREEN}No naming violations in: ${file#$PROJECT_ROOT/}${NC}"
                    else
                        # Apply fixes
                        if run_naming_fixes "$file"; then
                            ((fixed++))
                        else
                            ((failed++))
                        fi
                    fi
                fi
            done < <(get_source_files)
            
            echo ""
            echo -e "${GREEN}Completed processing $processed files${NC}"
            echo -e "${GREEN}Files with naming fixes applied: $fixed${NC}"
            if [[ $failed -gt 0 ]]; then
                echo -e "${YELLOW}$failed files had issues${NC}"
            fi
            ;;
        *)
            # Apply naming fixes to specific file or directory
            local target="$1"
            
            if [[ -f "$target" ]]; then
                # Single file
                if check_naming_violations "$(realpath "$target")"; then
                    echo -e "${GREEN}No naming violations found in: $target${NC}"
                else
                    echo -e "${YELLOW}Applying naming fixes...${NC}"
                    run_naming_fixes "$(realpath "$target")"
                fi
            elif [[ -d "$target" ]]; then
                # Directory
                echo -e "${YELLOW}Applying naming convention fixes to directory: $target${NC}"
                
                local processed=0
                local failed=0
                local fixed=0
                
                while IFS= read -r file; do
                    if [[ "$file" == "$(realpath "$target")"* ]] && [[ -f "$file" ]]; then
                        ((processed++))
                        
                        # First check if file has violations
                        if check_naming_violations "$file" >/dev/null 2>&1; then
                            echo -e "${GREEN}No naming violations in: ${file#$PROJECT_ROOT/}${NC}"
                        else
                            # Apply fixes
                            if run_naming_fixes "$file"; then
                                ((fixed++))
                            else
                                ((failed++))
                            fi
                        fi
                    fi
                done < <(get_source_files)
                
                echo ""
                echo -e "${GREEN}Completed processing $processed files in $target${NC}"
                echo -e "${GREEN}Files with naming fixes applied: $fixed${NC}"
                if [[ $failed -gt 0 ]]; then
                    echo -e "${YELLOW}$failed files had issues${NC}"
                fi
            else
                echo -e "${RED}Error: File or directory not found: $target${NC}"
                exit 1
            fi
            ;;
    esac
}

# Run main function
main "$@"