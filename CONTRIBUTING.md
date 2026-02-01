# Contributing to PixelTerm-C

Thank you for your interest in contributing to PixelTerm-C! This document provides guidelines and instructions for contributing to the project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Code Style Guidelines](#code-style-guidelines)
- [Documentation Guidelines](#documentation-guidelines)
- [Testing Requirements](#testing-requirements)
- [Submitting Changes](#submitting-changes)
- [Review Process](#review-process)

## Code of Conduct

By participating in this project, you agree to maintain a respectful and collaborative environment. Please:

- Be respectful and constructive in discussions
- Welcome newcomers and help them get started
- Focus on what is best for the project and community
- Show empathy towards other community members

## Getting Started

### Prerequisites

Before you begin, ensure you have the required dependencies installed:

```bash
# Ubuntu/Debian
sudo apt-get install libchafa-dev libglib2.0-dev libgdk-pixbuf2.0-dev \
    libavformat-dev libavcodec-dev libswscale-dev libavutil-dev \
    pkg-config build-essential

# Optional (book support):
sudo apt-get install libmupdf-dev

# Arch Linux
sudo pacman -S chafa glib2 gdk-pixbuf2 ffmpeg pkgconf base-devel

# Optional (book support):
sudo pacman -S mupdf
```

### Setting Up Development Environment

1. **Fork the repository** on GitHub

2. **Clone your fork:**
   ```bash
   git clone https://github.com/YOUR_USERNAME/PixelTerm-C.git
   cd PixelTerm-C
   ```

3. **Add upstream remote:**
   ```bash
   git remote add upstream https://github.com/zouyonghe/PixelTerm-C.git
   ```

4. **Build the project:**
   ```bash
   make
   ```

5. **Run tests:**
   ```bash
   make test
   ```

For more detailed development information, see [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Development Workflow

### 1. Create a Feature Branch

Always create a new branch for your work:

```bash
# Update your main branch
git checkout main
git pull upstream main

# Create a feature branch
git checkout -b feature/your-feature-name
# or
git checkout -b fix/issue-number-description
```

**Branch Naming Conventions:**
- `feature/` - New features (e.g., `feature/add-zoom-support`)
- `fix/` - Bug fixes (e.g., `fix/123-memory-leak`)
- `docs/` - Documentation changes (e.g., `docs/update-api-docs`)
- `test/` - Test additions/improvements (e.g., `test/add-renderer-tests`)
- `refactor/` - Code refactoring (e.g., `refactor/simplify-input-handling`)

### 2. Make Your Changes

- Write clear, concise code following our style guidelines
- Add or update tests as needed
- Update documentation if you change functionality
- Keep commits focused and atomic

### 3. Test Your Changes

Before submitting, ensure all tests pass:

```bash
# Run all tests
make test

# Build the project
make clean && make

# Test manually with sample images/videos
./pixelterm path/to/test/image.jpg
```

### 4. Commit Your Changes

Write clear, descriptive commit messages:

```bash
git add .
git commit -m "Add feature: support for WebP animated images

- Implement WebP decoder using libwebp
- Add frame timing support for animations
- Update documentation with WebP usage examples

Fixes #123"
```

**Commit Message Format:**
- First line: Brief summary (50 chars or less)
- Blank line
- Detailed description (wrap at 72 chars)
- Reference related issues (e.g., "Fixes #123", "Related to #456")

### 5. Push and Create Pull Request

```bash
# Push to your fork
git push origin feature/your-feature-name

# Create Pull Request on GitHub
```

## Code Style Guidelines

### General Principles

- **Consistency is key** - Follow the existing code style
- **Readability matters** - Write code that others can understand
- **Keep it simple** - Avoid unnecessary complexity

### C Code Style

#### Indentation and Formatting
- Use **4 spaces** for indentation (no tabs)
- Use **K&R brace style** (opening brace on same line)
- Maximum line length: **100 characters**

```c
// Good
void function_name(int param1, char *param2) {
    if (condition) {
        // code
    }
}

// Bad
void function_name(int param1, char *param2)
{
  if (condition)
  {
    // code
  }
}
```

#### Naming Conventions
- **Functions:** `snake_case` (e.g., `browser_create`, `renderer_destroy`)
- **Variables:** `snake_case` (e.g., `file_path`, `image_data`)
- **Constants/Macros:** `UPPER_CASE` (e.g., `MAX_PATH_LEN`, `DEFAULT_WIDTH`)
- **Structs:** `PascalCase` with typedef (e.g., `FileBrowser`, `ImageRenderer`)
- **Enums:** `PascalCase` for type, `UPPER_CASE` for values

```c
// Good naming examples
typedef struct {
    char *file_path;
    int image_count;
} FileBrowser;

typedef enum {
    ERROR_NONE = 0,
    ERROR_FILE_NOT_FOUND,
    ERROR_MEMORY_ALLOCATION
} ErrorCode;

FileBrowser* browser_create(void);
void browser_destroy(FileBrowser *browser);
```

#### Comments and Documentation

**Function Documentation (in header files):**
```c
/**
 * @brief Creates a new FileBrowser instance
 * 
 * Allocates and initializes a new FileBrowser for managing file
 * navigation and image browsing in a directory.
 * 
 * @return Pointer to newly created FileBrowser, or NULL on failure
 * 
 * @note Caller must free the returned pointer with browser_destroy()
 * @see browser_destroy()
 */
FileBrowser* browser_create(void);
```

**Inline Comments:**
```c
// Use single-line comments for brief explanations
// Always include a space after //

// Good
// Calculate the optimal grid layout

// Bad
//calculate grid layout
```

**Block Comments (for complex logic):**
```c
/*
 * Multi-line comments for complex algorithms or
 * important design decisions that need detailed
 * explanation.
 */
```

#### Code Organization
- One function declaration per line
- Group related functions together
- Use blank lines to separate logical sections
- Include headers in this order:
  1. System headers (`<stdio.h>`, etc.)
  2. Library headers (`<glib.h>`, `<chafa.h>`, etc.)
  3. Project headers (`"browser.h"`, etc.)

```c
#include <stdio.h>
#include <stdlib.h>

#include <glib.h>
#include <chafa.h>

#include "browser.h"
#include "renderer.h"
```

#### Memory Management
- Always check return values from allocations
- Free all allocated memory
- Use GLib memory functions when working with GLib types

```c
// Good
FileBrowser *browser = g_new0(FileBrowser, 1);
if (!browser) {
    return NULL;
}

// Later
g_free(browser);
```

#### Error Handling
- Return error codes or NULL for error conditions
- Log errors appropriately
- Clean up resources before returning on error

```c
ImageData* load_image(const char *path) {
    if (!path || !g_file_test(path, G_FILE_TEST_EXISTS)) {
        g_warning("File not found: %s", path ? path : "(null)");
        return NULL;
    }
    
    ImageData *data = g_new0(ImageData, 1);
    if (!data) {
        g_error("Failed to allocate memory");
        return NULL;
    }
    
    // Load image...
    if (load_failed) {
        g_free(data);
        return NULL;
    }
    
    return data;
}
```

## Documentation Guidelines

### What to Document

- **Public APIs** - All functions in header files
- **Complex Algorithms** - Non-obvious implementation details
- **Design Decisions** - Why something is done a certain way
- **Limitations** - Known issues or constraints
- **Usage Examples** - How to use new features

### Documentation Requirements

1. **Header Files** - Must have Doxygen comments for all public functions
2. **Implementation Files** - Should have comments explaining complex logic
3. **README Updates** - Update README.md if adding user-facing features
4. **CHANGELOG** - Add entry to CHANGELOG.md for all changes

### Doxygen Tags

Use these Doxygen tags consistently:

- `@brief` - Short description (required)
- `@param` - Parameter description (for each parameter)
- `@return` - Return value description
- `@note` - Additional notes
- `@warning` - Important warnings
- `@see` - Cross-references to related functions

## Testing Requirements

### Writing Tests

- **Test Coverage** - Add tests for all new functions
- **Test Naming** - Use descriptive test names: `test_browser_scan_directory_success`
- **Test Organization** - Group related tests together
- **Edge Cases** - Test error conditions and boundary cases

### Test Structure

```c
#include <glib.h>
#include "browser.h"

static void test_browser_create_success(void) {
    // Arrange
    
    // Act
    FileBrowser *browser = browser_create();
    
    // Assert
    g_assert_nonnull(browser);
    g_assert_cmpint(browser->file_count, ==, 0);
    
    // Cleanup
    browser_destroy(browser);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    
    g_test_add_func("/browser/create/success", test_browser_create_success);
    
    return g_test_run();
}
```

### Running Tests

```bash
# Run all tests
make test

# Run specific test
./tests/test_browser

# With verbose output
./tests/test_browser --verbose
```

### Test Requirements for PRs

- All existing tests must pass
- New features must include tests
- Bug fixes should include regression tests
- Aim for 70%+ code coverage for new code

## Submitting Changes

### Pull Request Guidelines

1. **Title** - Clear, concise description of changes
2. **Description** - Detailed explanation including:
   - What changes were made
   - Why changes were needed
   - How to test the changes
   - Related issues (if any)
3. **Tests** - Include test results or screenshots
4. **Documentation** - Update relevant documentation
5. **Checklist** - Complete the PR checklist

### Pull Request Checklist

Before submitting, ensure:

- [ ] Code follows style guidelines
- [ ] All tests pass (`make test`)
- [ ] New tests added for new features
- [ ] Documentation updated (if needed)
- [ ] Commit messages are clear and descriptive
- [ ] No unnecessary files included (build artifacts, etc.)
- [ ] Changes are minimal and focused
- [ ] All compiler warnings resolved

### Pull Request Template

When creating a PR, use this template:

```markdown
## Description
Brief description of changes

## Related Issues
Fixes #123

## Changes Made
- Change 1
- Change 2
- Change 3

## Testing
- [ ] Tested on Linux
- [ ] Tested on macOS
- [ ] All tests pass
- [ ] Manual testing completed

## Screenshots (if applicable)
[Add screenshots for UI changes]

## Checklist
- [ ] Code follows style guidelines
- [ ] Tests added/updated
- [ ] Documentation updated
- [ ] All tests pass
```

## Review Process

### What Happens Next

1. **Automated Checks** - CI runs tests and checks
2. **Code Review** - Maintainers review your code
3. **Feedback** - You may receive comments or change requests
4. **Revision** - Make requested changes
5. **Approval** - Once approved, your PR will be merged

### During Review

- **Be Responsive** - Address feedback promptly
- **Be Open** - Consider suggestions objectively
- **Be Patient** - Reviews may take time
- **Ask Questions** - Don't hesitate to ask for clarification

### After Merge

- **Sync Your Fork** - Update your fork with upstream changes
- **Clean Up** - Delete merged branches
- **Celebrate** - You've contributed to PixelTerm-C! 🎉

## Additional Resources

- [Development Guide](docs/DEVELOPMENT.md) - Detailed development information
- [Architecture Documentation](docs/ARCHITECTURE.md) - System architecture
- [Project Roadmap](docs/ROADMAP.md) - Future plans
- [Code Quality Analysis](docs/CODE_QUALITY_ANALYSIS.md) - Quality metrics

## Getting Help

- **Issues** - Check existing issues or create a new one
- **Discussions** - Use GitHub Discussions for questions
- **Documentation** - Read the docs in the `docs/` directory

## License

By contributing to PixelTerm-C, you agree that your contributions will be licensed under the LGPL-3.0 license.

---

Thank you for contributing to PixelTerm-C! Your efforts help make terminal image browsing better for everyone. 🖼️
