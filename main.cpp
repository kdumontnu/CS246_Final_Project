#include <unistd.h>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include "pin.H"
#include <set>
#include <map> 

//Disable any instrumentation and dump all the instructions in the test program that we care about
// ie. instuctions that write to a register.  
#define DUMP_INSTS_USED

//turn on printf debug messages
//#define PRINTF

using std::cout;
using std::cerr;;
using std::string;
using std::endl;
std::ostream * out = &cerr;
#define X_IN_Y(x,y) (y.find(x) != y.end())
#define FOR_X_IN_Y(x,y) for (auto x = y.begin(); x != y.end(); x++) 
#define OPCODE_IS(o, s) (OPCODE_StringShort(o).compare(s)==0)


/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,         "pintool",
                            "outfile", "tool.out", "Output file for the pintool");

KNOB<BOOL>   KnobPid(KNOB_MODE_WRITEONCE,                "pintool",
                            "pid", "0", "Append pid to output");

KNOB<UINT64> KnobLimit(KNOB_MODE_WRITEONCE,        "pintool",
                            "inst_limit", "1000000000", "Quit after executing x number of instructions");
                            
// KNOB<UINT64> KnobTableSize(KNOB_MODE_WRITEONCE,        "pintool",
//                             "size", "8", "Size of array entries");                             

enum RT{freg=1, i8reg=2, i16reg=3, i32reg=4, i64reg=5}; 
#define IS_FLOAT(type) ((type == freg))
std::string rt_name[] = {"", "Float Reg", "8 Bit Int", "16 Bit Int", "32 Bit Int", "64 Bit Int"};
std::set<REG> allreg;
std::map<REG, RT> regtype;


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
    std::vector<REG> read_regs;
    REG write_reg;
    RT datatype; 

    //Below are some statistics and other info about the instructions
    // these may be modified whenever the instruction is hit
    UINT64 hit_count; //number of times the instruction has been executed.
    regval last_value_seen;//What's the last value stored to the out register? ... used to measure value locality.
    bool flag; 

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

        hit_count = 0;
        last_value_seen = regval((void*)ZEROBUF, datatype); 
        flag = false;
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

    out << "Insts of interest: " << endl;
    FOR_X_IN_Y(i, inst_data){
        if(i->second->flag){
            out << i->second->disassembly << endl;
        }
    }
#endif

    out << "Instruction total: " << insts_executed << endl;
    if(limit_reached)
        out << "Reason: limit reached\n";
    else
        out << "Reason: fini\n";
    out << "Output:" << endl;

}


VOID value_predict(ADDRINT ins_ptr, INST_DATA* ins_data , PIN_REGISTER* ref){
   if(insts_executed > KnobLimit.Value()){
        cout << "Ending " << endl;
        PrintResults(true);
        PIN_ExitProcess(EXIT_SUCCESS);
    }
    insts_executed++;    
    ins_data->hit_count++;

    regval value_to_write = regval(ref, ins_data->datatype);
    if(1||OPCODE_IS(ins_data->opcode,"XOR")){
        cout << ins_data->disassembly << endl;

        cout << "Instruction " << (ins_ptr & 0xFFFF) << " wrote value (" <<  rt_name[ins_data->datatype]  <<"): " << value_to_write <<","  << ins_data->last_value_seen << " to " <<REG_StringShort(ins_data->write_reg)  <<endl; 

    }

    ins_data->last_value_seen = value_to_write; 
    //ref->byte[MAX_BYTES_PER_PIN_REG]

}



VOID Instruction(INS ins, void *v){
    //Let's assume instructions only write to one register.
    // if the instruction does write to multiple registers, the one we care about 
    // is the last register it writes to whose type is in regtype.
    REG write_reg = REG_INVALID_;
    REG reg_iterate;
    for(int i = 0; (reg_iterate = INS_RegW(ins,i)) != REG_INVALID_; i++){
        if(!X_IN_Y(reg_iterate, regtype)){ continue; } //if not (reg_iterate in regtype)
        write_reg = reg_iterate;
    }
    if(write_reg == REG_INVALID_){ return; }
    if(!INS_IsValidForIpointAfter(ins)){return;} //we want to insert the instrumentation after the instruction.

    if(!X_IN_Y(INS_Address(ins), inst_data)){
        inst_data[INS_Address(ins)] = new INST_DATA(ins);        
    }

    if(INS_IsMemoryRead(ins)){
        inst_data[INS_Address(ins)] -> flag = true;
    }

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
