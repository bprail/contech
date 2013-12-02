#!/usr/bin/env python

# Input:  Executable file produced by the marker front end (ContechMarkFE)
# Output: Allows simulators to look up machine code & memory addresses per basic block. 

import sys
import argparse
import os
from util import *

class Instruction:
	
	def __init__(self, addr, bytes, reads, writes, asm):
		# PC of the instruction
		self.addr = addr
		# String of machine code bytes
		self.bytes = bytes
		# Number of memory ops expected by the instruction
		self.reads = reads
		self.writes = writes
		# Human-readable assembly 
		self.asm = asm

	# x86 nop instructions of various lengths		
	nopBytes = dict()
	nopBytes[1] = "90"
	nopBytes[2] = "90 90"
	nopBytes[3] = "90 90 90"
	nopBytes[4] = "90 90 90 90"
	nopBytes[5] = "66 66 66 66 90"
	nopBytes[6] = "66 66 66 66 90 90"
	nopBytes[7] = "66 66 66 66 90 90 90"
	# Add more here as necessary
	
	# Replace this instruction with a nop
	def squash(self):
		length = len(self.bytes.split(" "))
		self.bytes = Instruction.nopBytes[length]
		self.reads = 0
		self.writes = 0
		self.asm = "NOP (replaced marker function)" 
		
	def __str__(self):
		return "{0} {1} {2} {3} {4}".format(self.reads, self.writes, self.addr, self.asm.split(' ')[0], self.bytes)
		

class BasicBlock:
	nextId = 0
	
	def __init__(self):
		self.id = BasicBlock.nextId
		BasicBlock.nextId += 1
		self.instructions = []
		self.reads = 0
		self.writes = 0
	
	def check(self):
		actualReadsInBlock = 0
		actualWritesInBlock = 0
		for i in self.instructions:
			actualReadsInBlock += i.reads
			actualWritesInBlock += i.writes
		if actualReadsInBlock < self.reads:
			print_error("Count mismatch for Basic Block {0}: {1} read markers, but {2} reads observed.".format(self.id, self.reads, actualReadsInBlock)) 
		if actualWritesInBlock < self.writes:
			print_error("Count mismatch for Basic Block {0}: {1} write markers, but {2} writes observed.".format(self.id, self.writes, actualWritesInBlock)) 
		
	def __str__(self):
		result = str(self.id) + "\n"
		for i in self.instructions:
			result += str(i) + "\n"
		return result + "\n"


def main():
	
	description="""
Parses an executable file that has been marked with the Contech Marker Compiler. 
Allows simulators to look up machine code & memory addresses per basic block.
"""
	parser = argparse.ArgumentParser(description)
	parser.add_argument("exe", help="The executable to parse")
	parser.add_argument("out", help="Name of output file")
	parser.add_argument("--debug", help="Turn on verbose debugging", default=False)
	args = parser.parse_args()
	
	if not os.path.exists(args.exe):
		print_error("Input file does not exist: " + args.exe)
		exit(1)
	
	# List of all basic blocks to output	
	BasicBlocks = []
	# Current block object
	currentBlock = None
	# Holds the id for the next basic block
	id = -1
	# Have we seen at least one marker call in this function?
	sawAtLeastOneMarkInThisFunction = False
	
	# Get raw disassembly	
	raw = pcall(["objdump -d -M intel --section=.text",args.exe], silent=True, suppressOutput=True)
	
	# Process one line at a time
	for line in raw.split("\n"):
					
		if args.debug:
			print line
			
		# The format of an instruction line is:
		#    addr: \t bytes \t asm
		
		data = line.split("\t")
		
		if len(data) == 1:
			# File format header
			if "file format" in line:
				bitMode = line.split("elf")[1][0:2]
				
			# Function header
			if "<" in line:
				if args.debug:
					print_header("Entering function " + line.split("<")[1].strip(">:"))
				
				# Terminate the block from the previous function
				if currentBlock != None and sawAtLeastOneMarkInThisFunction:
					currentBlock.check()
					BasicBlocks.append(currentBlock)
					if args.debug:
						print_header("Finished Block (from prev function):")
						print str(currentBlock)
					
		
				# Begin the first block of the new function
				id = -1	
				sawAtLeastOneMarkInThisFunction = False
				currentBlock = BasicBlock()
		
		# asm was blank -> Saw another line of bytes for the previous instruction
		elif len(data) == 2:
			currentBlock.instructions[-1].bytes += " " + data[1].strip(" ") 
		
		# Handle instruction
		elif len(data) == 3:
			
			# Parse out instruction from disassembly
			addr = data[0].strip(" :")
			bytes = data[1].strip(" ")
			asm = data[2]
			
			# Count number of memory accesses in the instruction
			reads = 0
			writes = 0
			if "nop" in asm:
				pass
			elif "cmp" in asm or "test" in asm or "call" in asm:
				reads = asm.count("PTR")
			elif "," in asm:
				# Intel syntax is dst, src
				# Assuming that all memory operands are prefaced by PTR (or ds:)
				# TODO Catch more evil ways to specify a memory operation
				reads = asm.split(",")[1].count("PTR")
				reads += asm.split(",")[1].count("ds:")
				writes = asm.split(",")[0].count("PTR")
				writes += asm.split(",")[1].count("ds:")
			
			
			# Create the instruction object 
			inst = Instruction(addr, bytes, reads, writes, asm)
			 
			# See if the instruction is setting the ID of the next basic block
			# This is ugly, since there are multiple ways the compiler may set the value and the instruction might not come right before the call
			# An error will be detected at the marker instruction if no id has been set by then
			try:
				if bitMode == "32":
					if "mov    DWORD PTR [esp]," in asm:
						id = int(asm.split(",")[1],16)
				elif bitMode == "64":
					if "mov    edi," in asm:
						id = int(asm.split(",")[1],16)
					elif "xor    edi,edi" in asm:
						id = 0
			except:
				pass 
			 

			# Beginning of basic block (and end of the previous block)
			if "__ctStoreBasicBlockMark" in asm:
				
				# Only create a new block if this is not the first marker in the function 
				if sawAtLeastOneMarkInThisFunction:
					
					# Add this block to the list
					currentBlock.check()
					BasicBlocks.append(currentBlock)
					if args.debug:
						print_header("Finished Block:")
						print str(currentBlock)
					
					# Begin a new basic block
					currentBlock = BasicBlock()
				
				# Set the new ID
				if id == -1:
					print_error("Error: Missing bbId for basic block at " + addr)
				currentBlock.id = id
				
			# Mem Read
			elif "__ctStoreMemReadMark" in asm:
				
				# Add memOp to basic block
				currentBlock.reads += 1
			
			# Mem Write
			elif "__ctStoreMemWriteMark" in asm:
				
				# Add memOp to basic block
				currentBlock.writes += 1
			
			# All marker instructions:
			if "__ctStoreBasicBlockMark" in asm or "__ctStoreMemReadMark" in asm or "__ctStoreMemWriteMark" in asm:	
				
				# Replace the instruction with a nop
				inst.squash()
				
				# Record that this function had instrumentation
				sawAtLeastOneMarkInThisFunction = True
			

			# Add the instruction to the current block 
			currentBlock.instructions.append(inst)		

						
	# Write output to file
	with open(args.out, "w") as outfile: 
		for bb in BasicBlocks:
			outfile.write(str(bb))


if __name__ == "__main__":
    main()

	

