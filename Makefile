default:
	mkdir -p build && g++ main.cpp -g -fno-exceptions -o ./build/main

clean:
	rm -rf build
