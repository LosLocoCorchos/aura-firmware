.PHONY: all test clean
PYTHON ?= python3
all: test
test:
	$(PYTHON) probar.py
clean:
	$(PYTHON) -c "import shutil; shutil.rmtree('build', ignore_errors=True)"
