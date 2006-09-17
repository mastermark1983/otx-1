#import <Cocoa/Cocoa.h>
#import <Kernel/mach/machine.h>
#import <objc/objc-class.h>

#import "LangDefs.h"
#import "Optimizations.h"
#import "StolenDefs.h"

// MethodInfo
typedef struct
{
	objc_method		m;
	objc_class		oc_class;
	objc_category	oc_cat;
	BOOL			inst;		// to determine '+' or '-'
}
MethodInfo;

// RegisterInfo
typedef struct
{
	union {
		UInt32		intValue;		// used with GPRs
		double		doubleValue;	// use with FPRs someday
	};
	BOOL			isValid;		// value can be trusted
	objc_class*		classPtr;
	objc_category*	catPtr;
}
RegisterInfo;

// LocalVarInfo
typedef struct
{
	RegisterInfo	regInfo;
	UInt16			localAddress;
}
LocalVarInfo;

// LineInfo
typedef struct
{
	UInt32	address;
	char	code[25];
	BOOL	isCode;
	BOOL	isFunction;
}
LineInfo;

// Adapted from http://darwinsource.opendarwin.org/DevToolsApr2004/cctools-499/libdyld/debug.h
typedef struct dyld_data_section
{
    void*			stub_binding_helper_interface;
    void*			_dyld_func_lookup;
    void*			start_debug_thread;
    mach_port_t		debug_port;
    thread_port_t	debug_thread;
    void*			dyld_stub_binding_helper;
//    unsigned long	core_debug;	// wrong size and ignored anyway
}
dyld_data_section;

/*	ThunkInfo

	http://developer.apple.com/documentation/DeveloperTools/Conceptual/MachOTopics/Articles/dynamic_code.html#//apple_ref/doc/uid/TP40002528-SW1

	This URL describes Apple's approach to PIC and indirect addressing in
	PPC assembly. The idea is to use the address of an instruction as a
	base address, from which some data can be referenced by some offset.
	The address of the next instruction is stored in the program counter
	register, which is not directly accessible by user-level code. Since
	it's not directly accessible, Apple uses CPU-specific techniques to
	access it indirectly.

	In PPC asm, they save the link register then use the bcl instruction
	to load the link register with the address of the following instruction.
	This saved address is then copied from the link register into some GP
	register, and the original link register is restored. Subsequent code
	can add an offset to the saved address to reference whatever data.

	In the x86 chip, the program counter is called the instruction pointer.
	The call and ret instructions modify the IP as a side effect. The call
	instruction pushes IP onto the stack and the ret instruction pops it.
	To exploit this behavior, gcc uses small functions whose purpose is
	to simply copy the IP from the stack into some GP register, like so:

___i686.get_pc_thunk.bx:
	+0	10001ffc  8b1c24		movl	(%esp,1),%ebx
	+3	10001fff  c3			ret

	This routine copies IP into EBX and returns. Subsequent code in the
	calling function can use EBX as a base address, same as above. Note the
	use of 'pc'(program counter) in the routine name, as opposed to IP or
	EIP. This use of the word 'thunk' is inconsistent with other definitions
	I've heard, but there it is. From what I've seen, the thunk can be stored
	in EAX, EBX, ECX, or EDX, and there can be multiple get_pc_thunk routines,
	each referencing one of these registers. EBX is the most popular, followed
	by ECX, EAX, and finally EDX. I have only seen EDX being used for this
	in Linux code, but otx allows for it.

	The PPC version of this behavior requires no function calls, and is
	fairly easy to spot. And in x86 code, when symbols have not been stripped,
	otool reports the ___i686.get_pc_thunk.bx calls like a champ. Our only
	problem occurs when symbols are stripped in x86 code. In that case, otool
	cannot display the name of the routine, only the address being call'd.
	This is why we need ThunkInfo's. otx makes 2 passes over otool's output.
	During the first pass, it recognizes the code pattern of these get_pc_thunk
	routines, and saves their addresses in an array of ThunkInfo's. Having
	this data available during the 2nd pass makes it possible to reference
	whatever data we need in the calling function.
*/

typedef struct
{
	UInt32	address;	// address of the get_pc_thunk routine
	SInt8	reg;		// register to which the thunk is being saved
}
ThunkInfo;

/*	Line

	Represents a line of text from otool's output. For each __text section,
	otool is called twice- with symbolic operands(-V) and without(-v). The
	resulting 2 text files are each read into a doubly-linked list of Line's.
	Each Line contains a pointer to the corresponding Line in the other list.
	The reason for this approach is due to otool's inaccuracy in guessing
	symbols. From comments in ofile_print.c:

		"Both a verbose (symbolic) and non-verbose modes are supported to aid
		in seeing the values even if they are not correct."

	With both versions on hand, we can choose the better one for each Line.
	The criteria for choosing is defined in chooseLine:. This does result in a
	slight loss of info, in the rare case that otool guesses correctly for
	any instruction that is not a function call.
*/

struct Line
{
	char*			chars;		// C string
	UInt32			length;		// C string length
	struct Line*	next;		// next line in this list
	struct Line*	prev;		// previous line in this list
	struct Line*	alt;		// "this" line in the other list
	LineInfo		info;		// details
};

#define Line	struct Line

// Constants that represent which section is being referenced, indicating
// likely data types.
enum {
	PointerType,		// C string in (__TEXT,__cstring)
	PStringType,		// Str255 in (__TEXT,__const)
	CFStringType,		// cf_string_object in (__TEXT,__cfstring)
	FloatType,			// float in (__TEXT,__literal4)
	DoubleType,			// double in (__TEXT,__literal8)
	DataGenericType,	// ? in (__DATA,__data)
	DataConstType,		// ? in (__DATA,__const)
	DYLDType,			// function ptr in (__DATA,__dyld)
	NLSymType,			// non-lazy symbol* in (__DATA,__nl_symbol_ptr)
	ImpPtrType,			// cf_string_object* in (__IMPORT,__pointers)
	OCGenericType,			// Obj-C types
	OCStrObjectType,	// objc_string_object in (__OBJC,__string_object)
	OCClassType,		// objc_class in (__OBJC,__class)
	OCModType,			// obj_module in (__OBJC,__module_info)
};

// Number of characters in each 'field', pre-entabified. Comment field is
// limited only by MAX_COMMENT_LENGTH. A single space per field is
// hardcoded in the snprintf format strings to prevent collisions.

// TextFieldWidths
typedef struct
{
	UInt16	offset;
	UInt16	address;
	UInt16	instruction;
	UInt16	mnemonic;
	UInt16	operands;
}
TextFieldWidths;

#define MAX_FIELD_SPACING		100		// spaces between fields
#define MAX_FORMAT_LENGTH		50		// snprintf() format string
#define MAX_OPERANDS_LENGTH		1000
#define MAX_COMMENT_LENGTH		2000
#define MAX_LINE_LENGTH			10000
#define MAX_TYPE_STRING_LENGTH	200		// for encoded ObjC data types
#define MAX_MD5_LINE			1024	// for the md5 pipe

// Refresh progress bar after processing this many lines.
#define PROGRESS_FREQ			2500

// Toggle these to print symbol descriptions to console.
#define _OTX_DEBUG_SYMBOLS_		0
#define _OTX_DEBUG_DYSYMBOLS_	0

// Options for cplus_demangle()
#define DEMANGLE_OPTS			\
	DMGL_PARAMS | DMGL_ANSI | DMGL_VERBOSE | DMGL_TYPES | DMGL_RET_POSTFIX

// ============================================================================

@interface ExeProcessor : NSObject
{
@protected
	// UI
	NSTextField*			mProgText;
	NSProgressIndicator*	mProgBar;

	// guts
	NSURL*				mOFile;				// exe on disk
	char*				mRAMFile;			// exe in RAM
	char*				mVerboseFile;
	UInt32				mVerboseFileSize;
	char*				mPlainFile;
	UInt32				mPlainFileSize;
	NSString*			mOutputFilePath;
	Line*				mVerboseLineListHead;
	Line*				mPlainLineListHead;
	UInt32				mNumLines;			// used only for progress
	mach_header*		mMachHeader;
	cpu_type_t			mArchSelector;
	UInt32				mArchMagic;
	BOOL				mExeIsFat;
	BOOL				mSwapped;
	UInt32				mLocalOffset;
	ThunkInfo*			mThunks;			// x86 only
	UInt32				mNumThunks;			//

	TextFieldWidths		mFieldWidths;

	// base pointers for indirect addressing
	SInt8				mCurrentThunk;		// x86 register identifier
	UInt32				mCurrentFuncPtr;	// PPC function address

	// symbols that point to functions
	nlist**				mFuncSyms;
	UInt32				mNumFuncSyms;

	// mach-O sections
	section_info		mCStringSect;
	section_info		mNSStringSect;
	section_info		mClassSect;
	section_info		mMetaClassSect;
	section_info		mIVarSect;
	section_info		mObjcModSect;
	section_info		mObjcSymSect;
	section_info		mLit4Sect;
	section_info		mLit8Sect;
	section_info		mTextSect;
	section_info		mCoalTextSect;
	section_info		mCoalTextNTSect;
	section_info		mConstTextSect;
	section_info		mDataSect;
	section_info		mCoalDataSect;
	section_info		mCoalDataNTSect;
	section_info		mConstDataSect;
	section_info		mDyldSect;
	section_info		mCFStringSect;
	section_info		mNLSymSect;
	section_info		mImpPtrSect;
	UInt32				mTextOffset;
	UInt32				mEndOfText;

	// Obj-C stuff
	section_info*		mObjcSects;
	UInt32				mNumObjcSects;
	MethodInfo*			mClassMethodInfos;
	UInt32				mNumClassMethodInfos;
	MethodInfo*			mCatMethodInfos;
	UInt32				mNumCatMethodInfos;
	objc_class*			mCurrentClass;
	objc_category*		mCurrentCat;
	LocalVarInfo*		mLocalSelves;
	UInt32				mNumLocalSelves;

	// dyld stuff
	UInt32			mAddrDyldStubBindingHelper;
	UInt32			mAddrDyldFuncLookupPointer;

	// saved prefs for speed
	BOOL		mShowLocalOffsets;
	BOOL		mShowDataSection;
	BOOL		mShowMethReturnTypes;
	BOOL		mShowIvarTypes;
	BOOL		mEntabOutput;
	BOOL		mDemangleCppNames;

	// saved strings
	char		mArchString[90];	// "-arch ppc" "-arch i386" etc.
	char		mLineCommentCString[MAX_COMMENT_LENGTH];
	char		mLineOperandsCString[MAX_OPERANDS_LENGTH];

	// C function pointers- see Optimizations.h and speedyDelivery
	BOOL	(*GetDescription)			(id, SEL, char*, const char*);
	BOOL	(*LineIsCode)				(id, SEL, const char*);
	BOOL	(*LineIsFunction)			(id, SEL, Line*);
	UInt32	(*AddressFromLine)			(id, SEL, const char*);
	void	(*CodeFromLine)				(id, SEL, Line*);
	void	(*CheckThunk)				(id, SEL, Line*);
	void	(*ProcessLine)				(id, SEL, Line*);
	void	(*ProcessCodeLine)			(id, SEL, Line**);
	void	(*PostProcessCodeLine)		(id, SEL, Line**);
	void	(*ChooseLine)				(id, SEL, Line**);
	void	(*EntabLine)				(id, SEL, Line*);
	char*	(*GetPointer)				(id, SEL, UInt32, UInt8*);
	void	(*CommentForLine)			(id, SEL, Line*);
	void	(*CommentForSystemCall)		(id, SEL);
	void	(*UpdateRegisters)			(id, SEL, Line*);
	char*	(*PrepareNameForDemangling)	(id, SEL, char*);

	objc_class*		(*ObjcClassPtrFromMethod)		(id, SEL, UInt32);
	objc_category*	(*ObjcCatPtrFromMethod)			(id, SEL, UInt32);
	MethodInfo*		(*ObjcMethodFromAddress)		(id, SEL, UInt32);
	BOOL			(*ObjcClassFromName)			(id, SEL, objc_class*, const char*);
	char*			(*ObjcDescriptionFromObject)	(id, SEL, const char*, UInt8);

	void	(*InsertLineBefore)			(id, SEL, Line*, Line*, Line**);
	void	(*InsertLineAfter)			(id, SEL, Line*, Line*, Line**);
	void	(*ReplaceLine)				(id, SEL, Line*, Line*, Line**);

	BOOL	(*FindSymbolByAddress)		(id, SEL, UInt32);
	BOOL	(*FindClassMethodByAddress)	(id, SEL, MethodInfo**, UInt32);
	BOOL	(*FindCatMethodByAddress)	(id, SEL, MethodInfo**, UInt32);
	BOOL	(*FindIvar)					(id, SEL, objc_ivar*, objc_class*, UInt32);
}

- (id)initWithURL: (NSURL*)inURL
		 progText: (NSTextField*)inText
		  progBar: (NSProgressIndicator*)inProg;

// processors
- (BOOL)processExe: (NSString*)inOutputFilePath
			  arch: (cpu_type_t)inArchSelector;
- (void)createVerboseFile: (NSURL**)outVerbosePath
			 andPlainFile: (NSURL**)outPlainPath;
- (BOOL)loadMachHeader;
- (void)loadLCommands;
- (void)loadSegment: (segment_command*)inSegPtr;
- (void)loadSymbols: (symtab_command*)inSymPtr;
- (void)loadDySymbols: (dysymtab_command*)inSymPtr;
- (void)loadObjcSection: (section*)inSect;
- (void)loadObjcModules;
- (void)loadCStringSection: (section*)inSect;
- (void)loadNSStringSection: (section*)inSect;
- (void)loadClassSection: (section*)inSect;
- (void)loadMetaClassSection: (section*)inSect;
- (void)loadIVarSection: (section*)inSect;
- (void)loadObjcModSection: (section*)inSect;
- (void)loadObjcSymSection: (section*)inSect;
- (void)loadLit4Section: (section*)inSect;
- (void)loadLit8Section: (section*)inSect;
- (void)loadTextSection: (section*)inSect;
- (void)loadCoalTextSection: (section*)inSect;
- (void)loadCoalTextNTSection: (section*)inSect;
- (void)loadConstTextSection: (section*)inSect;
- (void)loadDataSection: (section*)inSect;
- (void)loadCoalDataSection: (section*)inSect;
- (void)loadCoalDataNTSection: (section*)inSect;
- (void)loadConstDataSection: (section*)inSect;
- (void)loadDyldDataSection: (section*)inSect;
- (void)loadCFStringSection: (section*)inSect;
- (void)loadNonLazySymbolSection: (section*)inSect;
- (void)loadImpPtrSection: (section*)inSect;

// customizers
- (BOOL)processVerboseFile: (NSURL*)inVerboseFile
			  andPlainFile: (NSURL*)inPlainFile;
- (void)gatherLineInfos;
- (void)decodeMethodReturnType: (const char*)inTypeCode
						output: (char*)outCString;
- (void)getDescription: (char*)ioCString
			   forType: (const char*)inTypeCode;
- (BOOL)printDataSections;
- (void)printDataSection: (section_info*)inSect
				  toFile: (FILE*)outFile;
- (BOOL)lineIsCode: (const char*)inLine;
- (BOOL)lineIsFunction: (Line*)inLine;
- (UInt32)addressFromLine: (const char*)inLine;
- (void)codeFromLine: (Line*)inLine;
- (void)checkThunk: (Line*)inLine;
- (void)processLine: (Line*)ioLine;
- (void)processCodeLine: (Line**)ioLine;
- (void)postProcessCodeLine: (Line**)ioLine;
- (void)chooseLine: (Line**)ioLine;
- (void)entabLine: (Line*)ioLine;
- (char*)getPointer: (UInt32)inAddr
			outType: (UInt8*)dataType;
- (void)commentForLine: (Line*)inLine;
- (void)commentForSystemCall;
- (void)updateRegisters: (Line*)inLine;

- (void)insertMD5;
- (char*)prepareNameForDemangling: (char*)inName;

- (objc_class*)objcClassPtrFromMethod: (UInt32)inAddress;
- (objc_category*)objcCatPtrFromMethod: (UInt32)inAddress;
- (MethodInfo*)objcMethodFromAddress: (UInt32)inAddress;
- (BOOL)objcClass: (objc_class*)outClass
		 fromName: (const char*)inName;
- (char*)objcDescriptionFromObject: (const char*)inObject
							  type: (UInt8)inType;

// stolen from cctools, mostly
- (BOOL)getObjcSymtab: (objc_symtab*)outSymTab
			  andDefs: (void***)outDefs
		   fromModule: (objc_module*)inModule;
- (BOOL)getObjcClass: (objc_class*)outClass
			 fromDef: (UInt32)inDef;
- (BOOL)getObjcCategory: (objc_category*)outCat
				fromDef: (UInt32)inDef;
- (BOOL)getObjcMetaClass: (objc_class*)outClass
			   fromClass: (objc_class*)inClass;
- (BOOL)getObjcMethodList: (objc_method_list*)outList
			   andMethods: (objc_method**)outMethods
			  fromAddress: (UInt32)inAddress;

// Line list manipulators
- (void)insertLine: (Line*)inLine
			before: (Line*)nextLine
			inList: (Line**)listHead;
- (void)insertLine: (Line*)inLine
			 after: (Line*)prevLine
			inList: (Line**)listHead;
- (void)replaceLine: (Line*)inLine
		   withLine: (Line*)newLine
			 inList: (Line**)listHead;
- (BOOL)printLinesFromList: (Line*)listHead;
- (void)deleteLinesFromList: (Line*)listHead;

// binary searches
- (BOOL)findSymbolByAddress: (UInt32)inAddress;
- (BOOL)findClassMethod: (MethodInfo**)outMI
			  byAddress: (UInt32)inAddress;
- (BOOL)findCatMethod: (MethodInfo**)outMI
			byAddress: (UInt32)inAddress;
- (BOOL)findIvar: (objc_ivar*)outIvar
		 inClass: (objc_class*)inClass
	  withOffset: (UInt32)inOffset;

- (void)speedyDelivery;
- (void)printSymbol: (nlist)inSym;

@end
