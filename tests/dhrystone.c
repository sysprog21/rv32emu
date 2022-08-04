/*	EVERBODY:	Please read "APOLOGY" below. -rick 01/06/85
 *			See introduction in net.arch, or net.micro
 *
 *	"DHRYSTONE" Benchmark Program
 *
 *	Version:	C/1.1-mc, 23/04/2017
 *
 *  Author:		Michael Clark <michaeljclark@mac.com>
 *
 *			- Use gettimeofday instead of time/getrusage.
 *			- Changed K&R declarations to typesafe versions.
 *			- Fix bug and call Proc3 with the address of RecordPtr
 *			  to match the types in the original K&R declaration.
 *
 *	Version:	C/1.1, 12/01/84
 *
 *	Date:		PROGRAM updated 01/06/86, RESULTS updated 03/31/86
 *
 *	Author:		Reinhold P. Weicker,  CACM Vol 27, No 10, 10/84 pg. 1013
 *			Translated from ADA by Rick Richardson
 *			Every method to preserve ADA-likeness has been used,
 *			at the expense of C-ness.
 *
 *	Compile:	cc -O dry.c -o drynr			: No registers
 *			cc -O -DREG=register dry.c -o dryr	: Registers
 *
 *	Defines:	Defines are provided for old C compiler's
 *			which don't have enums, and can't assign structures.
 *			The time(2) function is library dependant; Most
 *			return the time in seconds, but beware of some, like
 *			Aztec C, which return other units.
 *			The LOOPS define is initially set for 50000 loops.
 *			If you have a machine with large integers and is
 *			very fast, please change this number to 500000 to
 *			get better accuracy.  Please select the way to
 *			measure the execution time using the TIME define.
 *			For single user machines, time(2) is adequate. For
 *			multi-user machines where you cannot get single-user
 *			access, use the times(2) function.  If you have
 *			neither, use a stopwatch in the dead of night.
 *			Use a "printf" at the point marked "start timer"
 *			to begin your timings. DO NOT use the UNIX "time(1)"
 *			command, as this will measure the total time to
 *			run this program, which will (erroneously) include
 *			the time to malloc(3) storage and to compute the
 *			time it takes to do nothing.
 *
 *	Run:		drynr; dryr
 *
 *	Results:	If you get any new machine/OS results, please send to:
 *
 *				ihnp4!castor!pcrat!rick
 *
 *			and thanks to all that do.  Space prevents listing
 *			the names of those who have provided some of these
 *			results.  I'll be forwarding these results to
 *			Rheinhold Weicker.
 *
 *	Note:		I order the list in increasing performance of the
 *			"with registers" benchmark.  If the compiler doesn't
 *			provide register variables, then the benchmark
 *			is the same for both and NOREG.
 *
 *	PLEASE:		Send complete information about the machine type,
 *			clock speed, OS and C manufacturer/version.  If
 *			the machine is modified, tell me what was done.
 *			On UNIX, execute uname -a and cc -V to get this info.
 *
 *	80x8x NOTE:	80x8x benchers: please try to do all memory models
 *			for a particular compiler.
 *
 *	APOLOGY (1/30/86):
 *		Well, I goofed things up!  As pointed out by Haakon Bugge,
 *		the line of code marked "GOOF" below was missing from the
 *		Dhrystone distribution for the last several months.  It
 *		*WAS* in a backup copy I made last winter, so no doubt it
 *		was victimized by sleepy fingers operating vi!
 *
 *		The effect of the line missing is that the reported benchmarks
 *		are 15% too fast (at least on a 80286).  Now, this creates
 *		a dilema - do I throw out ALL the data so far collected
 *		and use only results from this (corrected) version, or
 *		do I just keep collecting data for the old version?
 *
 *		Since the data collected so far *is* valid as long as it
 *		is compared with like data, I have decided to keep
 *		TWO lists- one for the old benchmark, and one for the
 *		new.  This also gives me an opportunity to correct one
 *		other error I made in the instructions for this benchmark.
 *		My experience with C compilers has been mostly with
 *		UNIX 'pcc' derived compilers, where the 'optimizer' simply
 *		fixes sloppy code generation (peephole optimization).
 *		But today, there exist C compiler optimizers that will actually
 *		perform optimization in the Computer Science sense of the word,
 *		by removing, for example, assignments to a variable whose
 *		value is never used.  Dhrystone, unfortunately, provides
 *		lots of opportunities for this sort of optimization.
 *
 *		I request that benchmarkers re-run this new, corrected
 *		version of Dhrystone, turning off or bypassing optimizers
 *		which perform more than peephole optimization.  Please
 *		indicate the version of Dhrystone used when reporting the
 *		results to me.
 *
 *	The following program contains statements of a high-level programming
 *	language (C) in a distribution considered representative:
 *
 *	assignments			53%
 *	control statements		32%
 *	procedure, function calls	15%
 *
 *	100 statements are dynamically executed.  The program is balanced with
 *	respect to the three aspects:
 *		- statement type
 *		- operand type (for simple data types)
 *		- operand access
 *			operand global, local, parameter, or constant.
 *
 *	The combination of these three aspects is balanced only approximately.
 *
 *	The program does not compute anything meaningfull, but it is
 *	syntactically and semantically correct.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define LOOPS 10000000

char Version[] = "1.1-mc";

typedef enum { Ident1, Ident2, Ident3, Ident4, Ident5 } Enumeration;

typedef int OneToThirty;
typedef int OneToFifty;
typedef char CapitalLetter;
typedef char String30[31];
typedef int Array1Dim[51];
typedef int Array2Dim[51][51];

struct Record {
    struct Record *PtrComp;
    Enumeration Discr;
    Enumeration EnumComp;
    OneToFifty IntComp;
    String30 StringComp;
};

typedef struct Record RecordType;
typedef RecordType *RecordPtr;
typedef int boolean;

void Proc0();
void Proc1(RecordPtr PtrParIn);
void Proc2(OneToFifty *IntParIO);
void Proc3(RecordPtr *PtrParOut);
void Proc4();
void Proc5();
void Proc6(Enumeration EnumParIn, Enumeration *EnumParOut);
void Proc7(OneToFifty IntParI1, OneToFifty IntParI2, OneToFifty *IntParOut);
void Proc8(Array1Dim Array1Par,
           Array2Dim Array2Par,
           OneToFifty IntParI1,
           OneToFifty IntParI2);
Enumeration Func1(CapitalLetter CharPar1, CapitalLetter CharPar2);
boolean Func2(String30 StrParI1, String30 StrParI2);

int main()
{
    Proc0();
    exit(0);
}

/*
 * Package 1
 */
int IntGlob;
boolean BoolGlob;
char Char1Glob;
char Char2Glob;
Array1Dim Array1Glob;
Array2Dim Array2Glob;
RecordPtr PtrGlb;
RecordPtr PtrGlbNext;

void Proc0()
{
    OneToFifty IntLoc1;
    OneToFifty IntLoc2;
    OneToFifty IntLoc3;
    char CharIndex;
    Enumeration EnumLoc;
    String30 String1Loc;
    String30 String2Loc;

    register unsigned int i;
    struct timeval starttime;
    struct timeval endtime;
    long benchtime;

    PtrGlbNext = (RecordPtr) malloc(sizeof(RecordType));
    PtrGlb = (RecordPtr) malloc(sizeof(RecordType));
    PtrGlb->PtrComp = PtrGlbNext;
    PtrGlb->Discr = Ident1;
    PtrGlb->EnumComp = Ident3;
    PtrGlb->IntComp = 40;
    strcpy(PtrGlb->StringComp, "DHRYSTONE PROGRAM, SOME STRING");
    Array2Glob[8][7] = 10; /* Was missing in published program */

    /*****************
    -- Start Timer --
    *****************/
    gettimeofday(&starttime, NULL);
    for (i = 0; i < LOOPS; ++i) {
        Proc5();
        Proc4();
        IntLoc1 = 2;
        IntLoc2 = 3;
        strcpy(String2Loc, "DHRYSTONE PROGRAM, 2'ND STRING");
        EnumLoc = Ident2;
        BoolGlob = !Func2(String1Loc, String2Loc);
        while (IntLoc1 < IntLoc2) {
            IntLoc3 = 5 * IntLoc1 - IntLoc2;
            Proc7(IntLoc1, IntLoc2, &IntLoc3);
            ++IntLoc1;
        }
        Proc8(Array1Glob, Array2Glob, IntLoc1, IntLoc3);
        Proc1(PtrGlb);
        for (CharIndex = 'A'; CharIndex <= Char2Glob; ++CharIndex)
            if (EnumLoc == Func1(CharIndex, 'C'))
                Proc6(Ident1, &EnumLoc);
        IntLoc3 = IntLoc2 * IntLoc1;
        IntLoc2 = IntLoc3 / IntLoc1;
        IntLoc2 = 7 * (IntLoc3 - IntLoc2) - IntLoc1;
        Proc2(&IntLoc1);
    }

    /*****************
    -- Stop Timer --
    *****************/

    gettimeofday(&endtime, NULL);
    benchtime = (endtime.tv_sec * 1000000 + endtime.tv_usec) -
                (starttime.tv_sec * 1000000 + starttime.tv_usec);
    printf("Dhrystone(%s), %ld passes, %ld microseconds, %ld DMIPS\n", Version,
           (unsigned long) LOOPS, benchtime,
           /* overflow free division by VAX DMIPS value (1757) */
           (LOOPS * 71) / (benchtime >> 3));
}

void Proc1(RecordPtr PtrParIn)
{
#define NextRecord (*(PtrParIn->PtrComp))

    NextRecord = *PtrGlb;
    PtrParIn->IntComp = 5;
    NextRecord.IntComp = PtrParIn->IntComp;
    NextRecord.PtrComp = PtrParIn->PtrComp;
    Proc3(&NextRecord.PtrComp);
    if (NextRecord.Discr == Ident1) {
        NextRecord.IntComp = 6;
        Proc6(PtrParIn->EnumComp, &NextRecord.EnumComp);
        NextRecord.PtrComp = PtrGlb->PtrComp;
        Proc7(NextRecord.IntComp, 10, &NextRecord.IntComp);
    } else
        *PtrParIn = NextRecord;

#undef NextRecord
}

void Proc2(OneToFifty *IntParIO)
{
    OneToFifty IntLoc;
    Enumeration EnumLoc;

    IntLoc = *IntParIO + 10;
    for (;;) {
        if (Char1Glob == 'A') {
            --IntLoc;
            *IntParIO = IntLoc - IntGlob;
            EnumLoc = Ident1;
        }
        if (EnumLoc == Ident1)
            break;
    }
}

void Proc3(RecordPtr *PtrParOut)
{
    if (PtrGlb != NULL)
        *PtrParOut = PtrGlb->PtrComp;
    else
        IntGlob = 100;
    Proc7(10, IntGlob, &PtrGlb->IntComp);
}

void Proc4()
{
    boolean BoolLoc;

    BoolLoc = Char1Glob == 'A';
    BoolLoc |= BoolGlob;
    Char2Glob = 'B';
}

void Proc5()
{
    Char1Glob = 'A';
    BoolGlob = false;
}

extern boolean Func3();

void Proc6(Enumeration EnumParIn, Enumeration *EnumParOut)
{
    *EnumParOut = EnumParIn;
    if (!Func3(EnumParIn))
        *EnumParOut = Ident4;
    switch (EnumParIn) {
    case Ident1:
        *EnumParOut = Ident1;
        break;
    case Ident2:
        if (IntGlob > 100)
            *EnumParOut = Ident1;
        else
            *EnumParOut = Ident4;
        break;
    case Ident3:
        *EnumParOut = Ident2;
        break;
    case Ident4:
        break;
    case Ident5:
        *EnumParOut = Ident3;
    }
}

void Proc7(OneToFifty IntParI1, OneToFifty IntParI2, OneToFifty *IntParOut)
{
    OneToFifty IntLoc;

    IntLoc = IntParI1 + 2;
    *IntParOut = IntParI2 + IntLoc;
}

void Proc8(Array1Dim Array1Par,
           Array2Dim Array2Par,
           OneToFifty IntParI1,
           OneToFifty IntParI2)
{
    OneToFifty IntLoc;
    OneToFifty IntIndex;

    IntLoc = IntParI1 + 5;
    Array1Par[IntLoc] = IntParI2;
    Array1Par[IntLoc + 1] = Array1Par[IntLoc];
    Array1Par[IntLoc + 30] = IntLoc;
    for (IntIndex = IntLoc; IntIndex <= (IntLoc + 1); ++IntIndex)
        Array2Par[IntLoc][IntIndex] = IntLoc;
    ++Array2Par[IntLoc][IntLoc - 1];
    Array2Par[IntLoc + 20][IntLoc] = Array1Par[IntLoc];
    IntGlob = 5;
}

Enumeration Func1(CapitalLetter CharPar1, CapitalLetter CharPar2)
{
    CapitalLetter CharLoc1;
    CapitalLetter CharLoc2;

    CharLoc1 = CharPar1;
    CharLoc2 = CharLoc1;
    if (CharLoc2 != CharPar2)
        return (Ident1);
    else
        return (Ident2);
}

boolean Func2(String30 StrParI1, String30 StrParI2)
{
    OneToThirty IntLoc;
    CapitalLetter CharLoc;

    IntLoc = 1;
    while (IntLoc <= 1)
        if (Func1(StrParI1[IntLoc], StrParI2[IntLoc + 1]) == Ident1) {
            CharLoc = 'A';
            ++IntLoc;
        }
    if (CharLoc >= 'W' && CharLoc <= 'Z')
        IntLoc = 7;
    if (CharLoc == 'X')
        return true;
    if (strcmp(StrParI1, StrParI2) > 0) {
        IntLoc += 7;
        return true;
    }
    return false;
}

boolean Func3(Enumeration EnumParIn)
{
    Enumeration EnumLoc;

    EnumLoc = EnumParIn;
    if (EnumLoc == Ident3)
        return true;
    return false;
}
