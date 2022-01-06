# HeapVisualizationTool
Pintool for heap memory visualization

![image](https://user-images.githubusercontent.com/48536307/148425849-8ceb73cb-90bd-4a55-bc8b-8db4606a327d.png)

Through the use of this heap visualizer is possible to execute and instrument any program, and printing on screen the status of the heap memory after each function of memory allocation or deallocation. In order to correctly instrument each program has been used Pin, the well known dynamic binary instrumentation tool developed by Intel. As you can see from the image, the tool shows the list of chunks in use or free, and the list of various bin (unsorted, small, large, fast or tcache). At the start of the pintool, depending on the inclusion or not of the flag "-s", it will be possible to decide whether to have directly a total print of all chunks and bin for each call to malloc, free etc., or whether to make an analysis step by step in which, for each call intercepted, the user will be asked whether to print only the chunks, only the bin, both, or go to the next call without making any print.

N.B. in the pdf file there is an excerpt of the thesis (in Italian) that explains better the functioning of the program
