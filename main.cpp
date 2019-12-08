#include <unistd.h>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include "pin.H"
#include <set>
#include <map> 
#include <cmath>

//Disable any instrumentation and dump all the instructions in the test program that we care about
// ie. instuctions that write to a register.  
//#define DUMP_INSTS_USED

//turn on printf debug messages
//#define PRINTF

using std::cout;
using std::cerr;;
using std::string;
using std::endl;
using std::pow;
std::ostream * out = &cerr;
#define X_IN_Y(x,y) (y.find(x) != y.end())
#define FOR_X_IN_Y(x,y) for (auto x = y.begin(); x != y.end(); x++) 
#define OPCODE_IS(o, s) (OPCODE_StringShort(o).compare(s)==0)
#define IS_ARITHMETIC(t) (t == XED_CATEGORY_AVX || t ==  XED_CATEGORY_AVX2 || t == XED_CATEGORY_BINARY \
                            || t == XED_CATEGORY_CONVERT || t == XED_CATEGORY_LOGICAL || t ==  XED_CATEGORY_LOGICAL_FP \
                            || t == XED_CATEGORY_MISC  || t ==  XED_CATEGORY_SETCC || t == XED_CATEGORY_SHIFT \
                            || t == XED_CATEGORY_SSE || t == XED_CATEGORY_X87_ALU )

// Category for instruction
enum INST_CAT {
  UNKNOWN,
  I_PURE_LOAD,
  I_LOAD_ARITH,
  I_PURE_ARITH,
  I_ARITH_1OP,
  I_ARITH_2OP,
  I_REG_MOV,
  F_PURE_LOAD,
  F_LOAD_ARITH,
  F_PURE_ARITH,
  F_REG_MOVE,
};

std::string INST_CAT_s[] = {
  "UNKNOWN",
  "I_PURE_LOAD",
  "I_LOAD_ARITH",
  "I_PURE_ARITH",
  "I_ARITH_1OP",
  "I_ARITH_2OP",
  "I_REG_MOV",
  "F_PURE_LOAD",
  "F_LOAD_ARITH",
  "F_PURE_ARITH",
  "F_REG_MOVE"
};

UINT8 CATEGORIES = sizeof(INST_CAT_s)/sizeof(INST_CAT_s[0]);

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,         "pintool",
                            "outfile", "tool.out", "Output file for the pintool");

KNOB<BOOL>   KnobPid(KNOB_MODE_WRITEONCE,                "pintool",
                            "pid", "0", "Append pid to output");

KNOB<UINT64> KnobLimit(KNOB_MODE_WRITEONCE,        "pintool",
                            "inst_limit", "1000000000", "Quit after executing x number of instructions");

KNOB<string> KnobInstCat(KNOB_MODE_WRITEONCE,         "pintool",
                            "inst_cat", "ALL", "What category of instructions?");

KNOB<UINT64> KnobTableSize(KNOB_MODE_WRITEONCE,        "pintool",
                            "size", "8", "Size of Value Prediction table in bits. Total length = 2**size");     
                            
// KNOB<UINT64> KnobTableSize(KNOB_MODE_WRITEONCE,        "pintool",
//                             "size", "8", "Size of array entries");                             

enum RT{freg=1, i8reg=2, i16reg=3, i32reg=4, i64reg=5}; 
#define IS_FLOAT(type) ((type == freg))
std::string rt_name[] = {"", "Float Reg", "8 Bit Int", "16 Bit Int", "32 Bit Int", "64 Bit Int"};
std::set<REG> allreg;
std::map<REG, RT> regtype;

/* ===================================================================== */
/* Value Prediction Unit                                                 */
/* ===================================================================== */
UINT8 VPT_BITS = KnobTableSize.Value(); // Number of bits to use as VPT address
UINT8 VPT_MASK = VPT_BITS - 1;          // Mask for VPT address table
UINT32 VPT_ENTREIS = pow(2, VPT_BITS);  // Length of VPT table

UINT8 ZEROBUF[] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

class regval{ 
public:
    //enum RVT{FLOAT, INT} tag; //float type or int type? 
    RT real_type; //type?
    union{
        UINT8 float_store[MAX_BYTES_PER_PIN_REG]; // to store any float (some floats can be loooooooomg)
        UINT64 value; //to store any int type 
    };
    regval(){}
    regval(void* p_to_val, RT type){ 
        if(IS_FLOAT(type)){
            value = *((UINT64*) p_to_val);
        }else{
            memcpy(float_store, p_to_val, MAX_BYTES_PER_PIN_REG);
        }
        real_type = type;
    } 
    bool operator==(const regval& other) {
        assert(real_type == other.real_type);
        switch(real_type){
            case freg:
                return (memcmp(float_store, other.float_store, MAX_BYTES_PER_PIN_REG) == 0);
                break;
            case i64reg:
                return (value == other.value);
                break;
            case i32reg:
                return ((value&0xFFFFFFFF) == (other.value&0xFFFFFFFF));
                break;
            case i16reg:
                return ((value&0xFFFF) == (other.value&0xFFFF));
                break;
            case i8reg:
                return ((value&0xFF) == (other.value&0xFF));
                break;
            default:
                assert(false);
        }
    }
}; 


// cout << a << endl
std::ostream& operator<<(std::ostream &strm, const regval &a) {
    switch(a.real_type){
        case freg:
            strm << "FLOAT(";
            for(int i = MAX_BYTES_PER_PIN_REG; i >= 0; i--){
                strm <<std::setfill('0') << std::setw(2) << std::hex << (UINT32) (a.float_store[i]);
            }
            strm  << ")";
            return strm;
            break;
        case i64reg:
            return strm << "INT(" << a.value << ")"; 
            break;
        case i32reg:
            return strm << "INT(" << (a.value&0xFFFFFFFF) << ")"; 
            break;
        case i16reg:
            return strm << "INT(" << (a.value&0xFFFF) << ")"; 
            break;
        case i8reg:
            return strm << "INT(" << (a.value&0xFF) << ")"; 
            break;
        default:
            assert(false);
    }
}


//global number of instructions executed
UINT64 insts_executed; 
//The data in this class are properties of the instruction itself
// One INST_DATA object is allocated per instruction we care about, which is initialized in the Instruction() function
// Some statistics regarding the instruction is also kept here.
class INST_DATA{
public: 
    //These values are constant once initialized.
    string disassembly; //INS_Disassemble(ins)
    UINT32 category; // pretty print using  CATEGORY_StringShort
    UINT32 opcode; // pretty print using OPCODE_StringShort
    UINT32 num_read_reg; //INS_MaxNumRRegs(ins) 
    std::vector<REG> read_regs;  // REG is pin datatype
    REG write_reg;  // output register
    RT datatype; 

    //Below are some statistics and other info about the instructions
    // these may be modified whenever the instruction is hit
    UINT64 hit_count; //number of times the instruction has been executed.
    UINT64 pred_success;
    regval last_value_seen;//What's the last value stored to the out register? 
    INST_CAT flag; 

    INST_DATA(INS ins){
        disassembly = INS_Disassemble(ins);
        category = INS_Category(ins);
        opcode = INS_Opcode(ins);
        num_read_reg = INS_MaxNumRRegs(ins);

        REG wr = REG_INVALID_;
        REG reg_iterate;
        for(int i = 0; (reg_iterate = INS_RegW(ins,i)) != REG_INVALID_; i++){
            if(!X_IN_Y(reg_iterate, regtype)){ continue; } //if not (reg_iterate in regtype)
            wr = reg_iterate;
        }
        write_reg = wr;
        datatype = regtype[wr];

        for(int i = 0; (reg_iterate = INS_RegR(ins,i)) != REG_INVALID_; i++){
            read_regs.push_back(reg_iterate);
        }

        hit_count = 0; //total number of times this instruction has been executed 
        pred_success = 0; //how many times does current value = last_value_seen? Value locality = pred_success / hit_count
        last_value_seen = regval((void*)ZEROBUF, datatype); 
        flag = UNKNOWN; //set the flag when registering this instruction if it is and instruction class we are testing
    }
};
std::map<ADDRINT, INST_DATA*> inst_data;

VOID populate_regs(){
    for(int rn = REG_INVALID_; rn != REG_LAST; rn++){
        REG reg = static_cast<REG>(rn);
        //Skip if it's a virtual reg 
        if(!REG_is_machine(reg)){
            continue;
        }
        
        //assuming the register classes are mutually exclusive 
        // eg. REG_is_fr(reg) and REG_is_gr32(reg) cannot be both true
        // on a 32 bit architecture, it'll have to be revised to omit gr64.
        if(REG_is_fr(reg)){
            allreg.insert(reg);
            regtype[reg] = freg;
        } else if(REG_is_gr8(reg)){
            allreg.insert(reg);
            regtype[reg] = i8reg;
        } else if(REG_is_gr16(reg)){
            allreg.insert(reg);
            regtype[reg] = i16reg;
        } else if(REG_is_gr32(reg)){
            allreg.insert(reg);
            regtype[reg] = i32reg;
        } else if(REG_is_gr64(reg)){
            allreg.insert(reg);
            regtype[reg] = i64reg;
        }
    }
    // for(auto i : allreg) {
    //     cout << REG_StringShort(i) << " is a " << rt_name[regtype[i]] <<endl;
    // }    

}

/* ===================================================================== */
static INT32 Usage()
{
    cerr << "This pin tool does value prediction\n";

    cerr << KNOB_BASE::StringKnobSummary();

    cerr << endl;
    return -1;
}

/* ===================================================================== */
VOID PrintResults(bool limit_reached)
{
    string output_file = KnobOutputFile.Value();
    if(KnobPid.Value()) output_file += "." + getpid();

    std::ofstream out(output_file.c_str(), std::ios_base::app);
    //if (!output_file.empty()) { out = new std::ofstream(output_file.c_str());}

#ifdef DUMP_INSTS_USED
    //maps instruction category found by CATEGORY_StringShort() to a list of instructions and number of operands 
    std::map<UINT32, std::set<std::pair<UINT32,UINT32>>> insts_of_category; 
    std::map<UINT32, RT> types_of_int; //type of the instruction
    FOR_X_IN_Y(i, inst_data){
        if(!X_IN_Y(i->second->category, insts_of_category)){
            insts_of_category[i->second->category] = std::set<std::pair<UINT32,UINT32>>();
        }
        insts_of_category[i->second->category].insert(std::make_pair(i->second->opcode, i->second->num_read_reg));
        types_of_int[i->second->opcode] = i->second->datatype;
    }

    out << "Instruction type stats: " << endl;
    FOR_X_IN_Y(i, insts_of_category){
        out << (CATEGORY_StringShort(i->first)) << ": ";
        FOR_X_IN_Y(j, i->second){
            out << OPCODE_StringShort(j->first) << "(" << j->second << ")[" << rt_name[types_of_int[j->first] ] << "]" << ", ";
        }
        out <<endl;
    }
#endif

    //Aggregates the value locality statistics from all instructions that have flag set. 
    UINT32 total_success = 0;
    UINT32 total_hit_count = 0;
    UINT32 success_per_category[CATEGORIES] = {0};
    UINT32 hit_per_category[CATEGORIES] = {0};
    out << "Degree of Value Locality: " << endl;
    FOR_X_IN_Y(i, inst_data){
        if(i->second->flag){
            total_success += i->second->pred_success;
            total_hit_count  +=  i->second->hit_count;
            out << i->second->disassembly << ": " << i->second->pred_success << "/" << i->second->hit_count << endl;
            success_per_category[i->second->flag] += i->second->pred_success;
            hit_per_category[i->second->flag] += i->second->hit_count;
        }
    }
    out << "Total : " << total_success << "/" << total_hit_count << endl;


    out << "Instruction total: " << insts_executed << endl;
    if(limit_reached)
        out << "Reason: limit reached\n";
    else
        out << "Reason: fini\n";
    out << "Output:" << endl;

    out << "OPERATION" << "|" << "SUCCESS COUNT" << "|" << "TOTAL_COUNT" << endl;
    for(short i = 0; i < CATEGORIES; i++) {
      out << INST_CAT_s[i] << "|" << success_per_category[i] << "|" << hit_per_category[i] << endl;
    }

}


VOID value_predict(ADDRINT ins_ptr, INST_DATA* ins_data , PIN_REGISTER* ref){
   if(insts_executed > KnobLimit.Value()){
        cout << "Ending " << endl;
        PrintResults(true);
        PIN_ExitProcess(EXIT_SUCCESS);
    }
    insts_executed++;    
    ins_data->hit_count++;

    // ref is pointer to area of memory
    regval value_to_write = regval(ref, ins_data->datatype);

#ifdef PRINTF
    cout << ins_data->disassembly << endl;

    cout << "IP " << (ins_ptr & 0xFFFF) << " wrote val (" <<  rt_name[ins_data->datatype]  <<"): " << value_to_write <<","  << ins_data->last_value_seen << " to " <<REG_StringShort(ins_data->write_reg)  <<endl; 
#endif

    if(value_to_write == ins_data->last_value_seen){
        ins_data->pred_success++; 
    } 
    ins_data->last_value_seen = value_to_write; 
}

INST_CAT set_instr_cat(INS ins, REG write_reg){
  if(INS_IsMemoryRead(ins) && !IS_FLOAT(regtype[write_reg]) && INS_Category(ins) == XED_CATEGORY_DATAXFER) { 
    return I_PURE_LOAD;
  } else if(INS_IsMemoryRead(ins) && !IS_FLOAT(regtype[write_reg]) && INS_Category(ins) != XED_CATEGORY_DATAXFER && IS_ARITHMETIC(INS_Category(ins))) {
    return I_LOAD_ARITH;
  } else if(!INS_IsMemoryRead(ins) && !IS_FLOAT(regtype[write_reg]) && INS_Category(ins) != XED_CATEGORY_DATAXFER && IS_ARITHMETIC(INS_Category(ins))) {
    return I_PURE_ARITH;
  } else if(!INS_IsMemoryRead(ins) && !IS_FLOAT(regtype[write_reg]) && INS_Category(ins) != XED_CATEGORY_DATAXFER && !IS_ARITHMETIC(INS_Category(ins)) && INS_MaxNumRRegs(ins) == 1) {
    return I_ARITH_1OP;
  } else if(!INS_IsMemoryRead(ins) && !IS_FLOAT(regtype[write_reg]) && INS_Category(ins) != XED_CATEGORY_DATAXFER && !IS_ARITHMETIC(INS_Category(ins)) && INS_MaxNumRRegs(ins) == 2) {
    return I_ARITH_2OP;
  } else if(!INS_IsMemoryRead(ins) && !IS_FLOAT(regtype[write_reg]) && INS_Category(ins) == XED_CATEGORY_DATAXFER) {
    return I_REG_MOV;
  } else if(INS_IsMemoryRead(ins) && IS_FLOAT(regtype[write_reg]) && INS_Category(ins) == XED_CATEGORY_DATAXFER) {
    return F_PURE_LOAD;
  } else if(INS_IsMemoryRead(ins) && IS_FLOAT(regtype[write_reg]) && INS_Category(ins) != XED_CATEGORY_DATAXFER && IS_ARITHMETIC(INS_Category(ins))) {
    return F_LOAD_ARITH;
  } else if(!INS_IsMemoryRead(ins) && IS_FLOAT(regtype[write_reg]) && INS_Category(ins) != XED_CATEGORY_DATAXFER && IS_ARITHMETIC(INS_Category(ins))) {
    return F_PURE_ARITH;
  } else if(!INS_IsMemoryRead(ins) && IS_FLOAT(regtype[write_reg]) && INS_Category(ins) == XED_CATEGORY_DATAXFER) {
    return F_REG_MOVE;
  } else {
    return UNKNOWN;
  }
}

VOID Instruction(INS ins, void *v){
    //Let's assume instructions only write to one register.
    // if the instruction does write to multiple registers, the one we care about 
    // is the last register it writes to whose type is in regtype.
    REG write_reg = REG_INVALID_; // Register ID instruction writes to
    REG reg_iterate;
    // Iterated each of the the registers
    // Swaps will write to multiple registers - might want to blacklist
    for(int i = 0; (reg_iterate = INS_RegW(ins,i)) != REG_INVALID_; i++){
        // Save the last register we write to
        if(!X_IN_Y(reg_iterate, regtype)){ continue; } //if not (reg_iterate in regtype)
        write_reg = reg_iterate;
    }
    if(write_reg == REG_INVALID_){ return; }
    // Checks that we can attach instrumentation after instruction (e.g. no branch)
    if(!INS_IsValidForIpointAfter(ins)){return;} //we want to insert the instrumentation after the instruction.
    
    // pin does not support some addresses
    if(REG_StringShort(write_reg).compare("mxcsr") == 0){return;}
    else if (REG_StringShort(write_reg).compare("st0") == 0){return;}
    else if (REG_StringShort(write_reg).compare("st1") == 0){return;}
    else if (REG_StringShort(write_reg).compare("st2") == 0){return;}
    else if (REG_StringShort(write_reg).compare("st3") == 0){return;}

    if(!X_IN_Y(INS_Address(ins), inst_data)){
        inst_data[INS_Address(ins)] = new INST_DATA(ins);        
    }

    // Set instruction category 
    inst_data[INS_Address(ins)] -> flag = set_instr_cat(ins, write_reg);

#ifndef DUMP_INSTS_USED
    INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR) value_predict, 
                        IARG_INST_PTR, 
                        IARG_PTR, inst_data[INS_Address(ins)],  // data of this instruction
                        IARG_REG_CONST_REFERENCE , write_reg, //pointer to register it writes to (PIN_REGISTER* )
                        IARG_END); 
#endif 
}

/* ===================================================================== */
VOID Fini(int n, void *v)
{
    PrintResults(false);
}

/* ===================================================================== */
int main(int argc, char *argv[])
{
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }

    insts_executed = 0;
    populate_regs();

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();

    return 0;
}
