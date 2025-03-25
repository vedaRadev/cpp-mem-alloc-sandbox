default:
	mkdir -p build && g++ main.cpp -o ./build/main

clean:
	rm -rf build
