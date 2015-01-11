#!/bin/bash

path=`pwd`
archive=$1
tmp=`mktemp -d`
cd $tmp
mkdir ofiles
cp $path/$archive .
master_count=`ar t $archive | wc -w`
#echo master_count=$master_count 
ar t $archive | xargs -n1 > master_list
#echo master_list: ` cat master_list`
count=0
subdir=1
comm_list=''
for files in `cat master_list | sort | uniq`
do
    n=`grep -w $files master_list | wc -l`
    if [ "$n" -gt 1 ]
    then
        #echo $files: $n
        for i in ` seq 1 $n`
        do
            comm_list=`echo $comm_list $files`
        done
    fi
done
#echo Repeated files: $comm_list
#echo Extracting blindly 
ar x $archive 
count=`ls *.o | wc -w`
#echo count=$count
mv *.o ofiles/
until [ "$master_count" == "$count" ]
do
    list=`echo $comm_list | xargs -n1 | sort | uniq`
    for files in $list
    do
        comm_list=`echo $comm_list | sed "s/$files//"`
    done
    if [ "$comm_list" != "" ]
    then
        #echo Pushing $comm_list to end of $archive to expose new ones
        ar m $archive $comm_list
        #echo Extracting $comm_list from $archive
        ar x $archive $comm_list
        #echo Creating archive with extracted $comm_list 
        ar rcs ${archive}_${subdir} $comm_list
        ar t ${archive}_${subdir}  | xargs -n1 > subdir_list
        #echo ${subdir}_list: `cat subdir_list`
        #comm_list=`grep -Fwf master_list subdir_list`
        subdir_count=`echo $comm_list | xargs -n1 | sort | uniq | wc -l`
        count=`echo $subdir_count + $count | bc -l`
        #echo count: $count
        subdir=`echo $subdir + 1 | bc -l`
        rename .o .$subdir.o *.o
        mv *.o ofiles/
    else
        break
    fi
done
cd $path
mv $tmp/ofiles/*.o .
rm -rf $tmp

