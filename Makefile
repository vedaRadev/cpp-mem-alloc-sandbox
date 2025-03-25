default:
	mkdir -p build && g++ main.cpp -g -o ./build/main

clean:
	rm -rf build
