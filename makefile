all:
	@echo "BUILDING: C++ Component"
	@cd src;node-waf configure build;cd ..
	@cp ./src/build/default/_ipcbuffer.node ./lib

distclean:
	rm -rf src/build/
	rm -rf *~
	rm -rf *.buf
	rm -rf src/.lock-wscript

clean: distclean
	rm -rf lib/_ipcbuffer.node

