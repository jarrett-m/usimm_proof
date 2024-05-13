# all_servers=(kestrel1 kestrel2 kestrel3 kestrel4 kestrel5 kestrel6 osprey1 osprey2 osprey3 osprey5 osprey6 osprey7 osprey8 osprey9 osprey10 osprey11 osprey12 osprey13 osprey14 osprey16 osprey17 osprey18)
# all_servers=(osprey6 osprey7 osprey8 osprey9 osprey10 osprey11 osprey12 osprey13 osprey14 osprey16 osprey17 osprey18 osprey1 osprey2 osprey3 osprey5 kestrel1 kestrel2 kestrel3 kestrel4 kestrel5 kestrel6 kestrel7 kestrel8)
# all_servers=(osprey10 osprey11 osprey12 osprey13 osprey14 osprey16 osprey17 osprey18 osprey1 osprey2 osprey3 osprey5 kestrel5 kestrel6 kestrel7 kestrel8 osprey6 osprey7 osprey8 osprey9 kestrel1 kestrel2 kestrel3 kestrel4)
all_servers=(kestrel1 kestrel2 kestrel3 kestrel4 kestrel5 kestrel6 kestrel7 kestrel8) # 
# all_servers=(phoenix1 phoenix2 phoenix3 phoenix4 phoenix5 phoenix6 kestrel3 kestrel4 kestrel5 kestrel6 kestrel7 kestrel8 kestrel2)

numservers=`echo "${#all_servers[@]}"`

USIMM_CFG=/home/sarab/WorkingDirectory/Simulators/Replacement_policy/usimminput/sarab_64kBMeta.cfg
# USIMM_CFG=/home/sarab/WorkingDirectory/Simulators/MACaggregation/usimminput/sarab_8kBMeta.cfg
# USIMM_CFG=/home/sarab/WorkingDirectory/Simulators/MACaggregation/usimminput/sarab_64kBMeta-Power.cfg

USIMM_TRACES=/uusoc/scratch/res/arch/students/meysam/cpu2017/four-core-traces/final
# USIMM_DIR=/home/sarab/WorkingDirectory/Simulators/MACaggregation
USIMM_DIR=/uusoc/scratch/res/arch/students/jarrett/what/

SimulationName="TEST"    # RepPolicy MAC_8_8_100M-10_1K_dyn2-1000000 MAC_512_512_100M-10_1K_7 SYN_1_0_1_0_0

usimmtypes="usimm_secure"
replacement_policies="LRU" #"LRU L_LRU SRRIP L_SRRIP"

# allbenchmarksuites="GAP NPB SPEC2017"
allbenchmarksuites="SPEC2017"
# GAP="bc  bfs  cc  pr  sssp  tc" # Orig
# GAP="bfs tc"
# all_NPB="bt  cg  dc  ep  ft  is  lu  mg  sp  ua"
# NPB="bt  cg  dc  ft  is  lu  mg  sp  ua" # Orig
# NPB="bt lu mg ua"
# all_SPEC2017="500.perlbench_r 503.bwaves_r 505.mcf_r 508.namd_r 520.omnetpp_r 527.cam4_r 531.deepsjeng_r 538.imagick_r 549.fotonik3d_r 554.roms_r 602.gcc_s 607.cactuBSSN_s 619.lbm_s 623.xalancbmk_s 657.xz_s"
# SPEC2017="500.perlbench_r 503.bwaves_r 505.mcf_r 520.omnetpp_r 527.cam4_r 531.deepsjeng_r 538.imagick_r 549.fotonik3d_r 554.roms_r 602.gcc_s 607.cactuBSSN_s 619.lbm_s 623.xalancbmk_s 657.xz_s" # Orig
# SPEC2017="503.bwaves_r 505.mcf_r 538.imagick_r 549.fotonik3d_r 554.roms_r 602.gcc_s 607.cactuBSSN_s"

# Selected bench with reasonable results
# GAP="bc  bfs  cc  sssp"
# NPB="bt  dc  ft  is"
# SPEC2017="500.perlbench_r 520.omnetpp_r 531.deepsjeng_r 549.fotonik3d_r 602.gcc_s 607.cactuBSSN_s 619.lbm_s 657.xz_s"
SPEC2017="520.omnetpp_r"
# leftovers
# GAP="bc cc pr sssp"
# NPB="cg  dc  ft  is  sp"
# SPEC2017="500.perlbench_r 520.omnetpp_r 527.cam4_r 531.deepsjeng_r 619.lbm_s 623.xalancbmk_s 657.xz_s"

# ssh jarrett@kestrel1.cs.utah.edu "ps -ef | grep usimm" 

MAX_PROOF_QUEUE=128
# # Check whether all servers are up
# for (( i=0; i<$numservers; i++ ))
# do
#     ssh jarrett@${all_servers[i]}.cs.utah.edu "echo ${all_servers[i]} in service"
# done

# create binaries
for usimmtype in $usimmtypes
do
    for replacement_policy in $replacement_policies
    do
        # Change replacement policy in the src config
        if [[ "$replacement_policy" == "LRU" ]]; then
            sed -i 's/define LRU_REP.*/define LRU_REP 1/g' $USIMM_DIR/$usimmtype/src/memory_controller.h
        elif [[ "$replacement_policy" == "L_LRU" ]]; then
            sed -i 's/define LRU_REP.*/define LRU_REP 0/g' $USIMM_DIR/$usimmtype/src/memory_controller.h
        elif [[ "$replacement_policy" == "SRRIP" ]]; then
            sed -i 's/define LRU_REP.*/define LRU_REP 2/g' $USIMM_DIR/$usimmtype/src/memory_controller.h
        elif [[ "$replacement_policy" == "L_SRRIP" ]]; then
            sed -i 's/define LRU_REP.*/define LRU_REP 3/g' $USIMM_DIR/$usimmtype/src/memory_controller.h
        else
            echo "Undefined metadata cache replacement policy."
            exit
        fi
        # For Scratchpad + Cache (No need to change config)
        
        sed -i "s/define MAX_PROOF_QUEUE .*/define MAX_PROOF_QUEUE ${MAX_PROOF_QUEUE}/g" $USIMM_DIR/$usimmtype/src/memory_controller.h

        cd $USIMM_DIR/$usimmtype
        mkdir -p $USIMM_DIR/$usimmtype/bin/$SimulationName

        if [[ $replacement_policy == "L_"* ]]; then     # If level based + other policy
            numvalues=`echo "${#all_alpha[@]}"`
            for (( i=0; i<$numvalues; i++ ))
            do
                    sed -i "s/define ALPHA.*/define ALPHA ${all_alpha[i]}/g" $USIMM_DIR/$usimmtype/src/memory_controller.h
                    sed -i "s/define BETA.*/define BETA ${all_beta[i]}/g" $USIMM_DIR/$usimmtype/src/memory_controller.h

                    # Make it (Weirdly, doesn't work if I don't clean first)
                    make clean -j100 > /dev/null
                    make -j10 1> /dev/null
                    # Move the binary to format $replacement_policy"_"$alpha"_"$beta
                    mv $USIMM_DIR/$usimmtype/bin/usimm $USIMM_DIR/$usimmtype/bin/$SimulationName/usimm_${replacement_policy}"_"${all_alpha[i]}"_"${all_beta[i]}
                    mkdir -p $USIMM_DIR/Results/$SimulationName/$usimmtype/$replacement_policy"_"${all_alpha[i]}"_"${all_beta[i]}
            done
        else
            # Make it (Weirdly, doesn't work if I don't clean first)
            make clean -j100 > /dev/null
            make -j10 1> /dev/null
            # Move the binary to format $replacement_policy
            mv $USIMM_DIR/$usimmtype/bin/usimm $USIMM_DIR/$usimmtype/bin/$SimulationName/usimm_${replacement_policy}
            mkdir -p $USIMM_DIR/Results/$SimulationName/$usimmtype/$replacement_policy
        fi
    done
done

echo "Binaries created. Running benchmarks now..."
currServer=0
numrunningProcs=1
MAXruns=`echo "( ${#all_servers[@]} * 8 )" | bc -l`         # Can run max of 7 commands per remote server

# # # Run baseline
# usimmtype="non_secure"
# cd $USIMM_DIR/$usimmtype
# for benchmarksuite in $allbenchmarksuites
# do
#     allbench="${benchmarksuite}"
#     for bench in ${!allbench}
#     do
#         ssh -x jarrett@${all_servers[currServer]}.cs.utah.edu "nohup $USIMM_DIR/$usimmtype/bin/usimm $USIMM_CFG $USIMM_TRACES/$benchmarksuite/$bench/core_0 $USIMM_TRACES/$benchmarksuite/$bench/core_1 $USIMM_TRACES/$benchmarksuite/$bench/core_2 $USIMM_TRACES/$benchmarksuite/$bench/core_3 1> $USIMM_DIR/Results/$usimmtype/${bench}.log 2>&1 &"
#         # Staggering jobs over available servers
#         if (( $currServer == $((numservers-1)) )) ; then
#             currServer=0
#         else
#             currServer=$((currServer+1))
#         fi
#         numrunningProcs=$((numrunningProcs+1))
#         # Servers overloaded. Wait till they finish
#         if (( $numrunningProcs == $MAXruns )) ; then
#             echo "Servers overloaded. Sleeping for 30m"
#             sleep 30m
#             #wait    # Need load balancing
#             numrunningProcs=1
#         fi

#     done
# done

> $USIMM_DIR/Results/status.log
for usimmtype in $usimmtypes
do
    for replacement_policy in $replacement_policies
    do
        cd $USIMM_DIR/$usimmtype

        if [[ $replacement_policy == "L_"* ]]; then     # If level based + other policy
            numvalues=`echo "${#all_alpha[@]}"`
            for (( i=0; i<$numvalues; i++ ))
            do
                for benchmarksuite in $allbenchmarksuites
                do
                    allbench="${benchmarksuite}"
                    for bench in ${!allbench}
                    do
                        echo $(date): ${all_servers[currServer]}: ${usimmtype}: ${replacement_policy}_${all_alpha[i]}_${all_beta[i]}: ${benchmarksuite} - ${bench} >> $USIMM_DIR/Results/status.log 2>&1
                        ssh -x jarrett@${all_servers[currServer]}.cs.utah.edu "nohup $USIMM_DIR/$usimmtype/bin/$SimulationName/usimm_${replacement_policy}"_"${all_alpha[i]}"_"${all_beta[i]} $USIMM_CFG $USIMM_TRACES/$benchmarksuite/$bench/core_0 $USIMM_TRACES/$benchmarksuite/$bench/core_1 $USIMM_TRACES/$benchmarksuite/$bench/core_2 $USIMM_TRACES/$benchmarksuite/$bench/core_3 1> $USIMM_DIR/Results/$SimulationName/$usimmtype/${replacement_policy}_${all_alpha[i]}_${all_beta[i]}/${bench}.log 2>&1 &" 
                        # Staggering jobs over available servers
                        if (( $currServer == $((numservers-1)) )) ; then
                            currServer=0
                        else
                            currServer=$((currServer+1))
                        fi
                        # Better, dynamic scheduling algo
                        loadonserver=`ssh jarrett@${all_servers[currServer]}.cs.utah.edu "ps -ef | grep usimm | wc -l"`
                        until [ $loadonserver -lt 10 ]
                        do
                            if (( $currServer == $((numservers-1)) )) ; then
                                currServer=0
                                echo "All servers overloaded. Sleeping for 10mins..."
                                sleep 10m
                            else
                                currServer=$((currServer+1))
                            fi
                            loadonserver=`ssh jarrett@${all_servers[currServer]}.cs.utah.edu "ps -ef | grep usimm | wc -l"`
                        done
                        # # Really bad static scheduling algo
                        # numrunningProcs=$((numrunningProcs+1))
                        # # Servers overloaded. Wait till they finish
                        # if (( $numrunningProcs == $MAXruns )) ; then
                        #     echo "Servers overloaded. Sleeping for 40m"
                        #     sleep 40m
                        #     #wait    # Need load balancing
                        #     numrunningProcs=1
                        # fi
                    done
                done
            done
        else
            for benchmarksuite in $allbenchmarksuites
            do
                allbench="${benchmarksuite}"
                for bench in ${!allbench}
                do
                    echo $(date): ${all_servers[currServer]}: ${usimmtype}: ${replacement_policy}: ${benchmarksuite} - ${bench} >> $USIMM_DIR/Results/status.log 2>&1
                    ssh -x jarrett@${all_servers[currServer]}.cs.utah.edu "nohup $USIMM_DIR/$usimmtype/bin/$SimulationName/usimm_${replacement_policy} $USIMM_CFG $USIMM_TRACES/$benchmarksuite/$bench/core_0 $USIMM_TRACES/$benchmarksuite/$bench/core_1 $USIMM_TRACES/$benchmarksuite/$bench/core_2 $USIMM_TRACES/$benchmarksuite/$bench/core_3 1> $USIMM_DIR/Results/$SimulationName/$usimmtype/${replacement_policy}/${bench}.log 2>&1 &"
                    # Staggering jobs over available servers
                    if (( $currServer == $((numservers-1)) )) ; then
                        currServer=0
                    else
                        currServer=$((currServer+1))
                    fi
                    # Better, dynamic scheduling algo
                    loadonserver=`ssh jarrett@${all_servers[currServer]}.cs.utah.edu "ps -ef | grep usimm | wc -l"`
                    until [ $loadonserver -lt 10 ]
                    do
                        if (( $currServer == $((numservers-1)) )) ; then
                            currServer=0
                            echo "All servers overloaded. Sleeping for 10mins..."
                            sleep 10m
                        else
                            currServer=$((currServer+1))
                        fi
                        loadonserver=`ssh jarrett@${all_servers[currServer]}.cs.utah.edu "ps -ef | grep usimm | wc -l"`
                    done
                    # # Really bad static scheduling algo
                    # numrunningProcs=$((numrunningProcs+1))
                    # # Servers overloaded. Wait till they finish
                    # if (( $numrunningProcs == $MAXruns )) ; then
                    #     echo "Servers overloaded. Sleeping for 40m"
                    #     sleep 40m
                    #     # wait    # Need load balancing
                    #     numrunningProcs=1
                    # fi
                done
            done
        fi
    done
done

# ------------------------------------------------------------------------------
# OLD experiment lists
# ------------------------------------------------------------------------------
# Baselines: # "non_secure RepPolicy RepPolicy_NoMACcache RepPolicy_NoReplayProtect"
# SecureDIMM not required: # "MAC_8_8_*_*"
# SecureDIMM required: # "MAC_*_*_*_*" # In order: (1) MAC_AGG_RD_FACTOR (2) MAC_AGG_WR_FACTOR (3) MAC_AGG_RD_TIMEOUT (4) MAC_VC-Size in Bytes/core

# non_secure - Without sensitive information protection
# RepPolicy - Baseline SGX/VAULT/Morphable implementation (with META cache caching MAC and CNT) | MAC_AGG*=0, MAC_VC=0, *CACHE_ON=1, NO_REPLAY_PROTECTION=0
# RepPolicy_NoMACcache - RepPolicy without META caching MAC | MAC_AGG*=0, MAC_VC=0, MAC_CACHE_ON=0, CNT_CACHE_ON=1, NO_REPLAY_PROTECTION=0
# RepPolicy_NoReplayProtect - RepPolicy without CNT used | MAC_AGG*=0, MAC_VC=0, MAC_CACHE_ON=1, CNT_CACHE_ON=0, NO_REPLAY_PROTECTION=1
# MAC_8_8_*_* - RepPolicy_NoMACcache with 8 MACs co-placed in 1 MAC block | MAC_AGG*=8, MAC_AGG_RD_TIMEOUT=*, MAC_VC=*, MAC_CACHE_ON=0, CNT_CACHE_ON=1, NO_REPLAY_PROTECTION=0

# "non_secure RepPolicy RepPolicy_NoMACcache RepPolicy_NoReplayProtect" 
# "MAC_8_8_128_0 MAC_8_8_256_0 MAC_8_8_512_0 MAC_8_8_1024_0 MAC_8_8_2048_0" -> gives optimal TIMEOUT=512 TODO: Check with 2048 if the perf drops
# "-MAC_8_8_TIMEOUT_0 MAC_8_8_TIMEOUT_256 MAC_8_8_TIMEOUT_512 MAC_8_8_TIMEOUT_1024 MAC_8_8_TIMEOUT_2048 MAC_8_8_1024_1024" -> gives optimal VCsize=1024Bytes; but optimal TIMEOUT changed to 1024
# "MAC_8_8_TIMEOUT_VCsize_NoReplayProtect" Run MAC_8_8_TIMEOUT_VCsize without replay protection; TIMEOUT_VCsize=1024_1024
# "MAC_256_8_TIMEOUT_VCsize MAC_8_256_TIMEOUT_VCsize MAC_256_256_TIMEOUT_VCsize MAC_512_512_TIMEOUT_VCsize" TIMEOUT_VCsize=1024_1024
# "MAC_256_256_*_VCsize MAC_512_512_*_VCsize" * = 1024, 2048, 4096, 8192
# "MAC_8192_8192_16777216_1024 MAC_16384_16384_16777216_1024_NoReplayProtect" Insane upper bound

# USIMM_CFG=/home/sarab/WorkingDirectory/Simulators/MACaggregation/usimminput/sarab_64kB_2ch.cfg # This config has 2 channels, where MAC goes to Ch[1], rest to Ch[0]
# USIMM_CFG=/home/sarab/WorkingDirectory/Simulators/MACaggregation/usimminput/sarab_64kB_2ch_allto0.cfg # This config has 2 channels, but everything goes to Ch[0]

# "non_secure" - non_secure with 2 ch, and data redirected to ch[0]
# "RepPolicy_NoMACcache_MAC_Ch RepPolicy_NoMACcache_MAC_Ch_NoReplayProtect" - RepPolicy_NoMACcache using the above config of MAC going to Ch[1]. Note Ch[1]'s T_DATA_TRANS interfere with Ch[0]'s timings.
# "MAC_Ch_2_2_2048_1024 MAC_Ch_4_4_2048_1024 MAC_Ch_8_8_2048_1024 MAC_Ch_64_64_16384_1024"
# "MAC_Ch_8_8_2048_1024_NoReplayProtect MAC_Ch_16384_16384_16777216_1024_NoReplayProtect"
# "RepPolicy_NoMACcache_MAC_Ch_VAT RepPolicy_NoMACcache_MAC_Ch_NoReplayProtect_VAT" # offset turned off (=0x000000) in VAT.c

# ------------------------------------------------------------------------------
# FOR MICRO 21 PAPER:
# ------------------------------------------------------------------------------
# "non_secure" - non_secure with 2 ch, and data redirected to ch[0]
# Baseline - "RepPolicy_All_Ch0 RepPolicy_All_Ch0_NoReplayProtect" RepPolicy with sarab_64kB_2ch_allto0.cfg
# "RepPolicy_All_Ch0_noMACCache RepPolicy_All_Ch0_noMACCache_NoReplayProtect"
    # Not needed # "RepPolicy_MAC_Ch RepPolicy_MAC_Ch_NoReplayProtect"
    # Not needed # "RepPolicy_NoMACcache_MAC_Ch RepPolicy_NoMACcache_MAC_Ch_NoReplayProtect"
# "MAC_8_8_100M-10_0_0 MAC_Ch_8_8_100M-10_0_0 MAC_8_8_100M-10_0_0-NRP"  MAC_8_8_100M* is with sarab_64kB_2ch_allto0.cfg.
# "MAC_Ch_8_8_100M-10_1K_0" MAC_Ch_Rd_Wr_TIMEOUT-ROBLimit_VCsize_PrefetchFactor - MAC_flush_on_ROB_head=10, and large TIMEOUT=100M, and MAC VC=1KB
# "MAC_Ch_7_8_100M-10_1K_1 MAC_Ch_7_8_100M-10_4K_1" Increase in MAC+Prefetch buffer size
# "MAC_Ch_6_8_100M-10_4K_2 MAC_Ch_5_8_100M-10_4K_3 MAC_Ch_4_8_100M-10_4K_4 MAC_Ch_8_8_100M-10_4K_10000" _10000 is opportuninstic prefetch
# "MAC_Ch_8_8_100M-10_1K_10000"
    # "MAC_Ch_8_8_100M-10_1K_1 MAC_Ch_8_8_100M-10_1K_2 MAC_Ch_8_8_100M-10_1K_3 MAC_Ch_8_8_100M-10_1K_4 MAC_Ch_8_8_100M-10_1K_5" Varying Prefetch degree
# "MAC_Ch_8_8_100M-10_4K_dyn2-1000000 MAC_Ch_8_8_100M-10_4K_dyn2-10000" Dynamic Pref with varying interval; have starting prefetch degree=2
# "MAC_Ch_8_8_100M-10_1K_dyn2-1000000 MAC_Ch_8_8_100M-10_1K_dyn2-10000"

    # "MAC_Ch_8_8_100M-10_4K_7 MAC_Ch_8_8_100M-10_4K_10000" # chaining 8 into 1 slot, and inserting 7 prefetches
# "MAC_Ch_8_8_100M-10_1K_7 MAC_Ch_8_8_100M-10_1K_10000"

# "MAC_Ch_64_8_100M-10_4K_ MAC_Ch_8_8_100M-10_4K_ MAC_Ch_8_8_100M-10_4K_ MAC_Ch_8_8_100M-10_4K_"
    # ---Yet to create folders
    # For chaining, chain all values into 1 slot, rest 7 are static prefetches. Code to chain all 64 into 1 slot
    # "MAC_Ch_512_8_100M-10_1K_7"  
    # "RepPolicy_PI MAC_Ch_8_8_100M-PI_1K_0 MAC_Ch_64_8_100M-PI_1K_0 MAC_Ch_512_512_100M-PI_1K_0" With PoisonIvy; prefetch off??
# TODO: "MAC_Ch_64_8_100M-10_1K_7 MAC_Ch_64_64_100M-10_1K_7 MAC_Ch_512_8_100M-10_1K_7 MAC_Ch_512_512_100M-10_1K_7 MAC_Ch_64_8_100M-PI_1K_7 MAC_Ch_512_8_100M-PI_1K_7 MAC_Ch_64_64_100M-PI_1K_7 MAC_Ch_512_512_100M-PI_1K_7"

# "MAC_Ch_8_8_100M-10_1K_0-NRP MAC_Ch_8_8_100M-10_1K_10000-NRP MAC_Ch_8_8_100M-10_1K_dyn2-1000000-NRP MAC_Ch_8_8_100M-10_1K_dyn2-10000-NRP"
# "MAC_Ch_8_8_100M-10_1K_7-NRP MAC_Ch_64_64_100M-10_1K_7-NRP MAC_Ch_512_512_100M-10_1K_7-NRP MAC_Ch_512_8_100M-10_1K_7-NRP" Running
# "MAC_Ch_64_8_100M-10_1K_10000-NRP MAC_Ch_64_8_100M-10_1K_7-NRP MAC_Ch_64_64_100M-10_1K_10000-NRP MAC_Ch_512_512_100M-10_1K_10000-NRP MAC_Ch_64_8_100M-PI_1K_7-NRP MAC_Ch_512_8_100M-PI_1K_7-NRP MAC_Ch_64_64_100M-PI_1K_7-NRP MAC_Ch_512_512_100M-PI_1K_7-NRP"
# Upperlimit sims: "MAC_Ch_100000000_100000000_100M-PI_1K_0 MAC_Ch_100000000_100000000_100M-PI_1K_0-NRP"

# Sensitivity studies: varying MAC_flush_on_ROB_head, MACVC size

# POWER:
# non_secure POW_RepPolicy POW_RepPolicy-NRP 
# POW_MAC_Ch_8_8_100M-10_1K_0 POW_MAC_Ch_8_8_100M-10_1K_dyn2-1000000 POW_MAC_Ch_512_512_100M-10_1K_7
# POW_MAC_Ch_8_8_100M-10_1K_0-NRP POW_MAC_Ch_8_8_100M-10_1K_dyn2-1000000-NRP POW_MAC_Ch_512_512_100M-10_1K_7-NRP

# SYNERGY baseline: (Run with 2ch_allto0 cfg)
# "SYN_1_0_0_0_0 SYN_1_0_1_0_0 SYN_1_0_0_0_0-NRP SYN_1_0_1_0_0-NRP" Encoding: SYN_, SYNERGY,NO_PARITY_CACHE,FG_PARITY_CACHE,SHARED_PARITY,ECCC
# ITESP baseline: (Run with 2ch_allto0 cfg)
# "ITESP_1 ITESP_2 ITESP_1-NRP ITESP_2-NRP" Encoding: ITESP_, SHR*
# "ITESP_1_MAC_Ch_8_8_100M-10_1K_0"
# "SYN SYN-NRP ITESP ITESP_MAC_Ch_8_8_100M-10_1K_0"