#include "pin.H"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sstream>
#include <bitset>

using namespace std;
#define RESET   "\033[0m"
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */
#define BLUE    "\033[34m"      /* Blue */
#define MAGENTA "\033[35m"      /* Magenta */
#define CYAN    "\033[36m"      /* Cyan */
#define WHITE   "\033[37m"      /* White */
#define BOLDRED     "\033[1m\033[31m"      /* Bold Red */
#define BOLDGREEN   "\033[1m\033[32m"      /* Bold Green */
#define BOLDYELLOW  "\033[1m\033[33m"      /* Bold Yellow */
#define BOLDBLUE    "\033[1m\033[34m"      /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m"      /* Bold Magenta */
#define BOLDCYAN    "\033[1m\033[36m"      /* Bold Cyan */
#define BOLDWHITE   "\033[1m\033[37m"      /* Bold White */

#define CHUNKS  "\033[36m"
#define MALL "\033[1m\033[34m"

/* ===================================================================== */
/* Names of malloc and free */
/* ===================================================================== */
#if defined(TARGET_MAC)
#define MALLOC "_malloc"
#define FREE "_free"
#else
#define MALLOC "malloc"
#define FREE "free"
#define CALLOC "calloc"
#define REALLOC "realloc"
#endif

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

std::ofstream TraceFile;

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "heapV.out", "specify trace file name");

KNOB<bool> StepByStep(KNOB_MODE_WRITEONCE, "pintool",
    "s", "0", "Step by step");

KNOB<bool> doublePrint(KNOB_MODE_WRITEONCE, "pintool",
    "d", "0", "Print before and after allocation");
/* ===================================================================== */


/* ===================================================================== */
/* Analysis routines                                                     */
/* ===================================================================== */

ADDRINT start;
ADDRINT* main_arena;
ADDRINT* heapBase;
ADDRINT* endHeap;
ADDRINT* arrayBins;
ADDRINT* fastBins;
ADDRINT* tcache;
ADDRINT topChunk;
ADDRINT lastFree;
ADDRINT lastMall; 
ADDRINT lastReall;
ADDRINT lastCall;  
bool first = true;
bool prev = false;        //true if previus chunk in use
bool memoryArea = false;
bool mainA = false;
bool printFree = false;
bool printAll = true; 

void getMainArena() {
  int fd = open("/usr/lib/debug//lib/x86_64-linux-gnu/libc-2.31.so", O_RDONLY);  //file descriptor
  //struct stat stat;
  //fstat(fd, &stat);

  off_t fsize;
  fsize = lseek(fd, 0, SEEK_END);
  
  char *base = (char *) mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);        
  Elf64_Ehdr *header = (Elf64_Ehdr *)base;    
  Elf64_Shdr *secs = (Elf64_Shdr*)(base+header->e_shoff);                        
  for (unsigned secinx = 0; secinx < header->e_shnum; secinx++) {                
    if (secs[secinx].sh_type == SHT_SYMTAB) {                                    
      Elf64_Sym *symtab = (Elf64_Sym *)(base+secs[secinx].sh_offset);            
      char *symnames = (char *)(base + secs[secs[secinx].sh_link].sh_offset);
      unsigned symcount = secs[secinx].sh_size/secs[secinx].sh_entsize;
      for (unsigned syminx = 0; syminx < symcount; syminx++) {
        if (strcmp(symnames+symtab[syminx].st_name, "main_arena") == 0) {
          void *mainarena = ((char *)start)+symtab[syminx].st_value;
          main_arena = (ADDRINT *) mainarena;
        }
      }
    }
  }
  close(fd);
}

VOID getTopChunk(){
  topChunk = main_arena[12];
}

VOID getHeapBase(){
  std::ifstream infile ("/proc/self/maps",std::ifstream::in);
  std::string line;
  void* heapB ;
  void* heapE ;
  while (std::getline(infile, line)){
    if(line.find("[heap]")!= std::string::npos){
      std::string::size_type pos = line.find('-');
      std::string::size_type pos2 = line.find(" ");
      string str = line.substr(0, pos);
      string str1 = line.substr(pos+1,pos2-pos-1 );
      std::stringstream ss;  
      ss << std::hex<<str;  
      ss >>heapB; 

      std::stringstream ss1;
      ss1 << std::hex<<str1;  
      ss1 >>heapE;  
    }
  }
  heapBase = (ADDRINT*)heapB;
  endHeap = (ADDRINT*)heapE;
}

ADDRINT sizeAMP(ADDRINT size){
  ADDRINT ad= size;
  stringstream s1;
  s1 << hex << ad;
  unsigned n;
  s1 >> n;
  bitset<64> b(n);
  
  prev = b[0];
  memoryArea = b[1];
  mainA = b[2];
  b[0] = 0;
  b[1] = 0;
  b[2] = 0;

  std::stringstream s2;
  s2<<b.to_ulong();
  ADDRINT result;
  s2  >> result;
  return result;
}

bool inTcache(ADDRINT* current){
  for(int i=0; i<=63; i++){
    if(tcache[i] != 0){
      if(tcache[i] == (ADDRINT)current+16){
        return true;
      }else{
        ADDRINT* currentTc = (ADDRINT*) tcache[i];
        while(currentTc[0] != 0 && (ADDRINT)heapBase < currentTc[0] && currentTc[0] < topChunk ){
          if(currentTc[0] == (ADDRINT)current+16){
            return true;
          }
          if(currentTc != (ADDRINT*) currentTc[0]){ //to avoid endless loops
            currentTc = (ADDRINT*) currentTc[0];
          }else{
            break;
          }
        }
      }
    } 
  }
  return false;
}

bool inFast(ADDRINT* current){
  for(int i=0; i<=9; i++){
    if(fastBins[i] != 0){
      if(fastBins[i] == (ADDRINT)current){
        return true;
      }else{
        ADDRINT* currentF = (ADDRINT*) fastBins[i];
        while(currentF[2] != 0 && (ADDRINT)heapBase < currentF[2] && currentF[2] < topChunk){
          if(currentF[2] == (ADDRINT)current){
            return true;
          }
          if(currentF != (ADDRINT*) currentF[2]){
            currentF = (ADDRINT*) currentF[2];
          }else{
            break;
          }
        }
      }
    } 
  }
  return false;
}


VOID printInfo(){
  cout << CHUNKS << "[-------------------------------------Info-------------------------------------]\n\n" << RESET;
  getTopChunk();
  cout<<"MainArena: \t" << hex << BLUE << setw(14) << (ADDRINT) main_arena<<"\n" << RESET;
  cout<<"HeapBase:  \t" << hex << BLUE << setw(14) << (ADDRINT) heapBase<<"\n" << RESET;
  cout<<"TopChunk:  \t" << hex << BLUE << setw(14) << topChunk<<"\n" << RESET;
  cout<<"FastBins:  \t" << hex << BLUE << setw(14) << (ADDRINT) fastBins<<"\n" << RESET;
  cout<<"Tcache:    \t" << hex << BLUE << setw(14) << (ADDRINT) tcache<<"\n\n" << RESET;
  cout << "\n";
}

VOID printChunks(){
  cout << CHUNKS << "[------------------------------------Chunks------------------------------------]\n\n" << RESET;
  ADDRINT* current = heapBase;
  bool corrupted = false;
  cout << "ADDRESS \t\t" << "SIZE \t\t" << " STATE \t\t" << RESET << "FD \t\t\t" << "BK" << "\n";
  while((ADDRINT)current != topChunk){
    ADDRINT size = sizeAMP(current[1]);
    ADDRINT* next = (ADDRINT*) ((ADDRINT)current + size); 
    if((ADDRINT)heapBase <= (ADDRINT)next && (ADDRINT)next < topChunk){
      sizeAMP(next[1]);
    }else if((ADDRINT)next != topChunk){
      corrupted = true;
    }

    if(!corrupted){
      bool tc = inTcache(current);
      bool fs = inFast(current);
      if(prev){
        if(tc){                               //il cast ad ADDRINT è fatto solo per una questione visiva nella stampa
          cout << BLUE << hex << setw(14) << (ADDRINT)current << "\t\t" << YELLOW << size << "\t\t" << MAGENTA << " Tcache\t\t"<< RESET << hex << setw(14) << current[2] << "\t\t" << "-" << "\n";
        }
        else if(fs){
          cout << BLUE << hex << setw(14) << (ADDRINT)current << "\t\t" << YELLOW << size << "\t\t" << MAGENTA << " Fast\t\t"<< RESET << hex << setw(14) << current[2] << "\t\t" << "-" << "\n";
        }else{
          cout << BLUE << hex << setw(14) << (ADDRINT)current << "\t\t" << YELLOW << size << "\t\t" << RED << " Used \t\t" << RESET << "- \t\t\t" << "-" << "\n";
        }
      }else{
        cout << BLUE << hex << setw(14) << (ADDRINT)current << "\t\t" << YELLOW << size << "\t\t" << GREEN << " Free \t\t"<< RESET << hex << setw(14) << current[2] << "\t\t" << current[3] << "\n";
      }
    }else{
        cout << BLUE << hex << setw(8) << (ADDRINT)current << "\t\t" << YELLOW << setw(5) << size << "\t\t" << BOLDRED << "Unknown \t\t  "<< RESET << hex << setw(14) << current[2] << "\t " << current[3] << "\n";
    }
    if(current != next && (ADDRINT)next < topChunk){
      current = next;
    }else{
      break;
    }
  }
  cout<<"\n\n";
}


VOID printBins(){	//Print arrayBins (unsorted, small and large), fast bins and tcache bins
  cout << CHUNKS << "[-------------------------------------Bins-------------------------------------]\n\n" << RESET;
  bool binCheck = false;

  //Unsorted
  if(arrayBins[1] != (ADDRINT)& (main_arena[12])){
    ADDRINT* first = (ADDRINT*) arrayBins[1];
    ADDRINT* last = (ADDRINT*) arrayBins[2];
    if(first == last){                   //there is only 1 unsorted
      cout << "Unsorted = " << hex << setw(14) << first << "\n";    
    }else{                                              //there are more than 1 unsorted
      ADDRINT* next = (ADDRINT*) first[2];
      cout << "Unsorted = " << hex << setw(14) << (ADDRINT) first;
      while(next != last){
        cout << " --> " << hex << setw(14) << (ADDRINT) next;
        next = (ADDRINT*) next[2];
      }
      cout << " --> " << hex << setw(14) << (ADDRINT) last << "\n";
    }
    cout<<"\n";
  }

  //Small 
  for(int i=3, j=0; i<=126; i=i+2, j++){
    if(arrayBins[i] != (ADDRINT)&arrayBins[i-2]){
      binCheck = true;
      ADDRINT* first = (ADDRINT*) arrayBins[i];
      ADDRINT* last = (ADDRINT*) arrayBins[i+1];
      if(first == last){
        cout << "Small[" << std::dec << j << "] = " << hex << setw(14) << (ADDRINT) first << "\n";
      }else{
        ADDRINT* next = (ADDRINT*) first[2];
        cout << "Small[" << std::dec << j << "] = " << hex << setw(14) << (ADDRINT) first;
        while(next != last){
          cout << " --> " << hex << setw(14) << (ADDRINT) next;
          next = (ADDRINT*) next[2];
        }
        cout << " --> " << hex << setw(14) << (ADDRINT) last << "\n";
      } 
    }
  }
  if(binCheck){
    cout<<"\n";
    binCheck = false;
  }
  

  //Large
  for(int i=127, j=0; i<=252; i=i+2, j++){
    if(arrayBins[i] != (ADDRINT)&(arrayBins[i-2])){
      binCheck = true;
      ADDRINT* first = (ADDRINT*) arrayBins[i];
      ADDRINT* last = (ADDRINT*) arrayBins[i+1];
      if(first == last){
        cout << "Large[" << std::dec << j << "] = " << hex << setw(14) << (ADDRINT) first << "\n";
      }else{
        ADDRINT* next = (ADDRINT*) first[2];
        cout << "Large[" << std::dec << j << "] = " << hex << setw(14) << (ADDRINT) first;
        while(next != last){
          cout << " --> " << hex << setw(14) << (ADDRINT) next;
          next = (ADDRINT*) next[2];
        }
        cout << " --> " << hex << setw(14) << (ADDRINT) last << "\n";
      } 
    }
  }
  if(binCheck){
    cout<<"\n";
    binCheck = false;
  }

  //Fast
  for(int i=0, j=0; i<=9; i++, j++){
    if(fastBins[i] != 0){
      binCheck = true;
      cout << "Fast[" << std::dec << j << "] = " << hex << setw(14) << fastBins[i] ;
      if((ADDRINT)heapBase < fastBins[i] && fastBins[i] < topChunk){
        ADDRINT* current = (ADDRINT*) fastBins[i];
        while(current[2] != 0){
          cout << " --> " << hex << setw(14) << current[2];
          if(current != (ADDRINT*) current[2] && (ADDRINT)heapBase < current[2] && current[2] < topChunk){
            current = (ADDRINT*) current[2];
          }else{
            break;
          }
        }
      }
    }
  }
  if(binCheck){
    cout<<"\n\n";
    binCheck = false;
  }
  
  //tcache
  for(int i=0; i<=63; i++){
    if(tcache[i] != 0){
      cout << "Tcache[" << std::dec << i << "] = " << hex << setw(14) << tcache[i];
      if((ADDRINT)heapBase < tcache[i] && tcache[i] < topChunk){
        ADDRINT* current = (ADDRINT*) tcache[i];
        while(current[0] != 0){
          cout << " --> " << hex << setw(14) << current[0];
          if(current != (ADDRINT*) current[0] && (ADDRINT)heapBase < current[0] && current[0] < topChunk){
            current = (ADDRINT*) current[0];
          }else{
            break;
          }
        }
      }
      cout << "\n";
    }
  }
  cout<<"\n\n";

}


VOID BeforeMalloc(ADDRINT size){
  if(printFree){
    string input;
    if(StepByStep.Value()) {
      cout << BOLDMAGENTA << "\nfree("<< lastFree <<").";
      cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
      std::cin >> input;
    }
    cout<< MALL << "\n::::::::::::::::::::::::::::::free("<< lastFree <<")::::::::::::::::::::::::::::::"<< RESET <<"\n\n";
    if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
      printInfo();
      printChunks();
    }
    if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
      printBins();
    }
    printFree = false;
  }
  if(doublePrint.Value() && !first){
    string input;
    if(StepByStep.Value()){
      cout << BOLDMAGENTA << "\nbefore malloc("<< dec << size <<").";
      cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
      std::cin >> input;
    }
    cout<< MALL << "\n::::::::::::::::::::::::::before malloc("<< hex << size <<"):::::::::::::::::::::::::::"<< RESET <<"\n\n";
    if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
      printInfo();
      printChunks();
    }
    if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
      printBins();
    }
  }
  TraceFile << "malloc(" << size << ")" << endl;
  lastMall = size;                             
}

VOID AfterMalloc(ADDRINT ret){
  if(first){
    getHeapBase();
    getMainArena();
    arrayBins = &(main_arena[13]); //main_arena + 0x68
    fastBins =  &(main_arena[2]);
    tcache =  &(heapBase[18]);    //heapBase + 0x90
    cout << showbase << internal << setfill('0') << "\n";
    printInfo(); 
    cout<< MALL << "\n::::::::::::::::::::::::::malloc("<< hex << lastMall <<") = "<<std::hex<<ret<<":::::::::::::::::::::::::::"<< RESET <<"\n\n";
    printChunks();
    first = false;
  }else{
    string input;
    if(StepByStep.Value()){
      cout << BOLDMAGENTA << "\nmalloc("<< dec << lastMall <<").";
      cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
      std::cin >> input;
    }
    cout<< MALL << "\n::::::::::::::::::::::::::malloc("<< hex << lastMall <<") = "<<std::hex<<ret<<":::::::::::::::::::::::::::"<< RESET <<"\n\n";
    if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
      printInfo();
      printChunks();
    }
    if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
      printBins();
    }
    
 
  }
  TraceFile << "  returns " << ret << endl;
}

VOID BeforeCalloc(ADDRINT num, ADDRINT size){
  if(printFree){
    string input;
    if(StepByStep.Value()){
      cout << BOLDMAGENTA << "\nfree("<< lastFree <<").";
      cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
      std::cin >> input;
    }
    cout<< MALL << "\n::::::::::::::::::::::::::::::free("<< lastFree <<")::::::::::::::::::::::::::::::"<< RESET <<"\n\n";
    if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
      printInfo();
      printChunks();
    }
    if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
      printBins();
    }
    printFree = false;
  } 
  if(doublePrint.Value() && !first){
    string input;
    if(StepByStep.Value()){
      cout << BOLDMAGENTA << "\nbefore calloc("<< dec << num <<", "<< hex << size <<")."; 
      cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
      std::cin >> input;
    }
    cout<< MALL << "\n::::::::::::::::::::::::::before calloc("<< dec << num<<", "<< hex << size <<"):::::::::::::::::::::::::::"<< RESET <<"\n\n";
    if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
      printInfo();
      printChunks();
    }
    if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
      printBins();
    }
  }
  TraceFile << "calloc(" <<num << ", " << size << ")" << endl;
  lastCall = num;
  lastMall = size; 
}

VOID AfterCalloc(ADDRINT ret){
  if(first){
    main_arena = main_arena;
    getHeapBase();
    getMainArena();
    arrayBins = &(main_arena[13]); //main_arena + 0x68
    fastBins =  &(main_arena[2]);
    tcache =  &(heapBase[18]);    //heapBase + 0x90
    cout << showbase << internal << setfill('0') << "\n";
    printInfo(); 
    cout<< MALL << "\n::::::::::::::::::::::::::calloc("<< lastMall <<") = "<<std::hex<<ret<<":::::::::::::::::::::::::::"<< RESET <<"\n\n";
    printChunks();
    first = false;
  }else{
    string input;
    if(StepByStep.Value()){
      cout << BOLDMAGENTA << "\ncalloc("<< dec << lastMall <<")."; 
      cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
      std::cin >> input;
    }
    cout<< MALL << "\n::::::::::::::::::::::::::calloc("<< dec << lastCall<<", "<< hex << lastMall <<") = "<<std::hex<<ret<<":::::::::::::::::::::::::::"<< RESET <<"\n\n";
    if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
      printInfo();
      printChunks();
    }
    if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
      printBins();
    }
  }
  TraceFile << "  returns " << ret << endl;
}

VOID BeforeRealloc(ADDRINT addr, ADDRINT size){
  if(printFree){
    string input;
    if(StepByStep.Value()){
      cout << BOLDMAGENTA << "\nfree("<< lastFree <<").";
      cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
      std::cin >> input;
    }
    cout<< MALL << "\n::::::::::::::::::::::::::::::free("<< lastFree <<")::::::::::::::::::::::::::::::"<< RESET <<"\n\n";
    if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
      printInfo();
      printChunks();
    }
    if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
      printBins();
    }
    printFree = false;
  }
  if(doublePrint.Value()){
    string input;
    if(StepByStep.Value()){
      cout << BOLDMAGENTA << "\nbefore ralloc("<< size <<").";
      cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
      std::cin >> input;
    }
    cout<< MALL << "\n::::::::::::::::::::::::::before realloc("<< addr << ", "<<size <<"):::::::::::::::::::::::::::"<< RESET <<"\n\n";
    if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
      printInfo();
      printChunks();
    }
    if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
      printBins();
    }
  }
  TraceFile << "realloc(" << addr << ", " << size << ")" << endl;
  lastMall = size; 
  lastReall = addr;
}

VOID AfterRealloc(ADDRINT ret){
  string input;
  if(StepByStep.Value()){
    cout << BOLDMAGENTA << "\nralloc("<< lastMall <<").";
    cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
    std::cin >> input;
  }
  cout<< MALL << "\n::::::::::::::::::::::::::realloc("<< lastReall << ", "<<lastMall <<") = "<<std::hex<<ret<<":::::::::::::::::::::::::::"<< RESET <<"\n\n";
  if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
    printInfo();
    printChunks();
  }
  if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
    printBins();
  }
  TraceFile << "  returns " << ret << endl;
}

VOID BeforeFree(ADDRINT size, THREADID threadid){
  if(printFree){
    string input;
    if(StepByStep.Value()){
      cout << BOLDMAGENTA << "\nfree("<< lastFree <<").";
      cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
      std::cin >> input;
    }
    cout<< MALL << "\n::::::::::::::::::::::::::::::free("<< lastFree <<")::::::::::::::::::::::::::::::"<< RESET <<"\n\n";
    if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
      printInfo();
      printChunks();
    }
    if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
      printBins();
    }
    printFree = false;
  }  
  if(doublePrint.Value()){
    string input;
    if(StepByStep.Value()){
      cout << BOLDMAGENTA << "\nbefore free("<< size <<").";
      cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
      std::cin >> input;
    }
    cout<< MALL << "\n::::::::::::::::::::::::::::::before free("<< size <<")::::::::::::::::::::::::::::::"<< RESET <<"\n\n";
    if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
      printInfo();
      printChunks();
    }
    if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
      printBins();
    }
  }
  printFree = true;
  lastFree = size;
  TraceFile << "free(" << size << ")" << endl;
}


/* ===================================================================== */
/* Instrumentation routines                                              */
/* ===================================================================== */
   
VOID Image(IMG img, VOID *v){
  cout<< "Loading " << IMG_Name(img) << ", Image id = " << IMG_Id(img) << "\n";
  
  if(IMG_Name(img) == "/lib/x86_64-linux-gnu/libc.so.6"){
    
    start = IMG_LowAddress(img);
    RTN mallocRtn = RTN_FindByName(img, MALLOC);
    if (RTN_Valid(mallocRtn)){
      RTN_Open(mallocRtn);
      RTN_InsertCall(mallocRtn, IPOINT_BEFORE, (AFUNPTR)BeforeMalloc,
                      IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
      RTN_InsertCall(mallocRtn, IPOINT_AFTER, (AFUNPTR)AfterMalloc,
                      IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
      RTN_Close(mallocRtn);
    }

    // Find the free() function.
    RTN freeRtn = RTN_FindByName(img, FREE);
    if (RTN_Valid(freeRtn)){
      RTN_Open(freeRtn);
      // Instrument free() to print the input argument value.
      RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR)BeforeFree,
                      IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
      RTN_Close(freeRtn);
    }

    //Find the calloc() function
    RTN callocRtn = RTN_FindByName(img, CALLOC);
    if (RTN_Valid(callocRtn))
    {
        RTN_Open(callocRtn);

        // Instrument callocRtn to print the input argument value and the return value.
        RTN_InsertCall(callocRtn, IPOINT_BEFORE, (AFUNPTR)BeforeCalloc,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_END);
        RTN_InsertCall(callocRtn, IPOINT_AFTER, (AFUNPTR)AfterCalloc,
                       IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);

        RTN_Close(callocRtn);
    }

    //Find the realloc() function
    RTN reallocRtn = RTN_FindByName(img, REALLOC);
    if (RTN_Valid(reallocRtn))
    {
        RTN_Open(reallocRtn);

        // Instrument malloc() to print the input argument value and the return value.
        RTN_InsertCall(reallocRtn, IPOINT_BEFORE, (AFUNPTR)BeforeRealloc,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_END);
        RTN_InsertCall(reallocRtn, IPOINT_AFTER, (AFUNPTR)AfterRealloc,
                       IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);

        RTN_Close(reallocRtn);
    }
    
  }
}


/* ===================================================================== */

VOID Fini(INT32 code, VOID *v){
  if(printFree){
    string input;
    if(StepByStep.Value()){
      cout << BOLDMAGENTA << "\nfree("<< lastFree <<")."; 
      cout << " Print chunks (c), bins (b), all (a) or go to the next instruction (n)?" << RESET;
      std::cin >> input;
    }
    cout<< MALL << "\n::::::::::::::::::::::::::::::free("<< lastFree <<")::::::::::::::::::::::::::::::"<< RESET <<"\n\n";
    if(!StepByStep.Value() || input == "chunks" || input == "c" || input == "a"){
      printInfo();
      printChunks();
    }
    if(!StepByStep.Value() || input == "bins" || input == "b" || input == "a"){
      printBins();
    }
    printFree = false;
  }
  
  TraceFile.close();
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
   
INT32 Usage(){
  cerr << "This tool print heap changes on each malloc, realloc, calloc and free." << endl;
  cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
  return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[]){

  // Initialize pin & symbol manager
  PIN_InitSymbols();
  if( PIN_Init(argc,argv) )
  {
      return Usage();
  }
  
  // Write to a file since cout and cerr maybe closed by the application
  TraceFile.open(KnobOutputFile.Value().c_str());
  TraceFile << hex;
  TraceFile.setf(ios::showbase);

  // Register Image to be called to instrument functions.
  IMG_AddInstrumentFunction(Image, 0);
  PIN_AddFiniFunction(Fini, 0);

  // Never returns
  PIN_StartProgram();
  
  return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
