#!/bin/bash

# Script to apply clang-tidy fixes to files in the LinuxCam project
# Usage: ./apply_clang_tidy.sh [options] [file_or_directory]
#        ./apply_clang_tidy.sh --check-all  (generate comprehensive report)
#        ./apply_clang_tidy.sh --check file.cpp  (check single file without fixes)
#        ./apply_clang_tidy.sh file.cpp  (apply fixes to single file)
#        ./apply_clang_tidy.sh  (apply fixes to all source files)

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
    echo "Usage: $0 [options] [file_or_directory]"
    echo ""
    echo "Options:"
    echo "  --check-all       Generate comprehensive report of all violations"
    echo "  --check FILE      Check single file without applying fixes"
    echo "  --help            Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 --check-all                    # Generate full report"
    echo "  $0 --check src/main.cpp           # Check single file"
    echo "  $0 src/main.cpp                   # Apply fixes to single file"
    echo "  $0 include/LinuxFace/             # Apply fixes to directory"
    echo "  $0                                # Apply fixes to all source files"
}

# Function to get all source files
get_source_files() {
    find "$PROJECT_ROOT" \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) \
        -not -path "*/external/*" \
        -not -path "*/build/*" \
        -not -path "*/.git/*" | \
        sort
}

# Function to run clang-tidy on a single file
run_clang_tidy() {
    local file="$1"
    local check_only="$2"
    local relative_file="${file#$PROJECT_ROOT/}"
    
    echo -e "${BLUE}Processing: $relative_file${NC}"
    
    if [[ "$check_only" == "true" ]]; then
        # Check only, don't apply fixes
        clang-tidy \
            --config-file="$PROJECT_ROOT/.clang-tidy" \
            -p "$BUILD_DIR" \
            "$file" \
            2>&1 | sed "s|$PROJECT_ROOT/||g"
    else
        # Apply fixes
        clang-tidy \
            --config-file="$PROJECT_ROOT/.clang-tidy" \
            -p "$BUILD_DIR" \
            --fix \
            --fix-errors \
            "$file" \
            2>&1 | sed "s|$PROJECT_ROOT/||g"
    fi
    
    local exit_code=${PIPESTATUS[0]}
    if [[ $exit_code -eq 0 ]]; then
        echo -e "${GREEN}✓ Completed: $relative_file${NC}"
    else
        echo -e "${YELLOW}⚠ Issues found in: $relative_file${NC}"
    fi
    
    return $exit_code
}

# Function to generate comprehensive report
generate_report() {
    echo -e "${BLUE}Generating comprehensive clang-tidy report...${NC}"
    echo "===========================================" 
    echo "CLANG-TIDY COMPREHENSIVE ANALYSIS REPORT"
    echo "Date: $(date)"
    echo "==========================================="
    echo ""
    
    local total_files=0
    local files_with_issues=0
    local total_issues=0
    
    while IFS= read -r file; do
        if [[ -f "$file" ]]; then
            ((total_files++))
            echo ""
            echo "=== File $total_files: ${file#$PROJECT_ROOT/} ==="
            
            local output
            output=$(run_clang_tidy "$file" "true" 2>&1)
            
            if echo "$output" | grep -q "warning:\|error:"; then
                ((files_with_issues++))
                local file_issues
                file_issues=$(echo "$output" | grep -c "warning:\|error:" || echo "0")
                ((total_issues += file_issues))
                echo "$output"
            else
                echo "✓ No issues found"
            fi
        fi
    done < <(get_source_files)
    
    echo ""
    echo "==========================================="
    echo "SUMMARY:"
    echo "Total files analyzed: $total_files"
    echo "Files with issues: $files_with_issues"
    echo "Total issues found: $total_issues"
    echo "Compliance rate: $(( (total_files - files_with_issues) * 100 / total_files ))%"
    echo "==========================================="
}

# Main logic
main() {
    cd "$PROJECT_ROOT"
    
    case "${1:-}" in
        --help)
            show_usage
            exit 0
            ;;
        --check-all)
            generate_report
            ;;
        --check)
            if [[ -z "${2:-}" ]]; then
                echo -e "${RED}Error: --check requires a file argument${NC}"
                show_usage
                exit 1
            fi
            
            local file="$2"
            if [[ ! -f "$file" ]]; then
                echo -e "${RED}Error: File not found: $file${NC}"
                exit 1
            fi
            
            run_clang_tidy "$(realpath "$file")" "true"
            ;;
        "")
            # Apply fixes to all files
            echo -e "${YELLOW}Applying clang-tidy fixes to all source files...${NC}"
            echo -e "${YELLOW}This will modify files in place. Make sure you have backups!${NC}"
            read -p "Continue? (y/N): " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                echo "Aborted."
                exit 0
            fi
            
            local processed=0
            local failed=0
            
            while IFS= read -r file; do
                if [[ -f "$file" ]]; then
                    ((processed++))
                    if ! run_clang_tidy "$file" "false"; then
                        ((failed++))
                    fi
                fi
            done < <(get_source_files)
            
            echo ""
            echo -e "${GREEN}Completed processing $processed files${NC}"
            if [[ $failed -gt 0 ]]; then
                echo -e "${YELLOW}$failed files had issues${NC}"
            fi
            ;;
        *)
            # Apply fixes to specific file or directory
            local target="$1"
            
            if [[ -f "$target" ]]; then
                # Single file
                run_clang_tidy "$(realpath "$target")" "false"
            elif [[ -d "$target" ]]; then
                # Directory
                echo -e "${YELLOW}Applying clang-tidy fixes to directory: $target${NC}"
                
                local processed=0
                local failed=0
                
                while IFS= read -r file; do
                    if [[ "$file" == "$target"* ]] && [[ -f "$file" ]]; then
                        ((processed++))
                        if ! run_clang_tidy "$file" "false"; then
                            ((failed++))
                        fi
                    fi
                done < <(get_source_files)
                
                echo ""
                echo -e "${GREEN}Completed processing $processed files in $target${NC}"
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