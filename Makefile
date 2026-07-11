.PHONY: format tidy lint test

format:
	clang-format -i $$(find include src tests \( -name '*.hpp' -o -name '*.cpp' \))

tidy:
	clang-tidy -p build $$(find include src \( -name '*.hpp' -o -name '*.cpp' \))

lint: format tidy

test:
	cd build && \
	cmake .. && \
	cmake --build . && \
	ctest --output-on-failure