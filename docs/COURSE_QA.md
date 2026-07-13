# Course Project Answers

1.  [cite_start]"The goal is a Christmas launch, so timely delivery is critical for this MVP (no deadline extensions)."... This is written in the Introduction section of the Course project doc. [cite: 11] [cite_start]Does this mean we cannot use late days? [cite: 12]

    > [cite_start]**[AG]** No, you can use late days; in accordance with the usual course policies, as shared before. [cite: 13]

2.  [cite_start]Say one client is editing a particular file while someone else is streaming the same file. [cite: 14] [cite_start]How shall it output? [cite: 15]

    > [cite_start]**[AG]** - As long as the `WRITE` is not completed (the client has sent `ETIRW`), the file content remains the original content. [cite: 16] [cite_start]So, `STREAM` should display the original file content. [cite: 17]

3.  [10] Get Additional Information: Users can access a wealth of supplementary information about specific files. [cite_start]This includes details such as file size, access rights, timestamps, and other metadata, providing users with comprehensive insights into the files they interact with. [cite: 18, 19] [cite_start]What does timestamps mean in this case? [cite: 20]

    > [cite_start]**[AG]** - Time of file creation, last edited time and anything more you would like to add. [cite: 21]

4.  [cite_start]In example 4: write to a file: [cite: 22]
    ```
    Client: WRITE mouse.txt 2 # Inserting a sentence delimiter
    Client: 5 and AAD.
    aaaah # New sentences: [I dont like T-T PNS and AAD.]* [aaaah].
    Currently active status remains with the index at index 2 Client: 0 But, # New sentence
    : [But, I dont like T-T PNS and AAD.]* [aaaah].
    Client: ETIRW Write Successful!,
    ```
    [cite_start][cite: 22, 23, 24, 25]
    [cite_start]shouldn't the sentence number be 1 and not 2 in the `WRITE` command? [cite: 25] (because up until the previous line, we havent added a delimiter to sentence 1) [cite_start]and the active status also remains at sentence number 1 right? [cite: 26]

    > **[AG]** - Ah yes, true. [cite_start]It should be 1 only (mb) [cite: 27]

5.  [cite_start]In Example 10: Access Control: `Client: ADDACCESS -W nuh_uh.txt user3 Access granted successfully!` [cite: 28]
    [cite_start]`-> File: feedback.txt -> Owner: user1 -> Created: 2025-10-10 14:21 -> Last Modified: 2025-10-10 14:32 -> Size: 52 bytes -> Access: user1 (RW), user2 (RW) -> Last Accessed: 2025-10-10 14:32 by user1`, [cite: 29]
    [cite_start]isn't "File:" supposed to display the file's name (which is nuh_uh.txt)? [cite: 29] [cite_start]and in "Access:" why isn't user3 added (assuming there exists user3 coz the message says "Access granted successfully")? [cite: 30]

    > [cite_start]**[AG]** - Yes yes user3 is a typo, it was meant to be user2 only. [cite: 31] [cite_start]Will fix, thnx for pointing out! [cite: 32]

6.  [cite_start]"After each sentence write update, the index must word\_index must update for the next sentence." [cite: 37] [cite_start]Could you please explain what this sentence means [cite: 38]

    > **[AG]** - Fixed the wording, pls go through that. [cite_start]Also, an example issue (with possible solution) was shared in the 18th Oct tutorial. [cite: 39] [cite_start]So, refer to that for more clarity. [cite: 40]

7.  [cite_start]Just to confirm, `-W` flag would provide BOTH read and write access to the user right? [cite: 41]
    > [cite_start]**[AG]** - Yes [cite: 41]

8.  [cite_start]Referring to the word-index in example 4. Should we take it as 0-indexed or 1-indexed? [cite: 42] [cite_start]And is the index referring to a particular word or the positions between the words? [cite: 43] [cite_start]For example, say a sentence has " $E^{\prime\prime}$ will "1 Z" result in "A Z BCDE" and O Z result in "ZABCD E"? There are 4 cases of inserting a word using word index in example 4, sentence 0 word 4 and word 6, and sentence 1 word 5 and word 0, and the policy for inserting words doesn't match in all the cases. [cite: 44]

    > [cite_start]**[AG]** - The example assumes 0-index, but feel free to choose whatever suits you. [cite: 45] [cite_start]While there shouldn't be any disparity in the provided examples (I'll reconfirm and fix, if any), but the indexing and index-values can be chosen by you in whatever system you prefer, as long as the underlying structure for `WRITE` calls is preserved. [cite: 46]

9.  [cite_start]Under Bonus Functionalities, in Hierarchical Folder Structure, is the structure created by a user expected to be persistent? [cite: 47] [cite_start]i.e. when the user logs back in, he should start off with the folder structure he left with? [cite: 48]

    > [cite_start]**[AG]** - Not necessarily, you can have them starting from the root folder. [cite: 49]

10. Under Bonus Functionalities, do checkpoints need to be persistent? [cite_start]Also, are checkpoints file specific or user specific? [cite: 50]

    > [cite_start]**[AG]** - Yes, that's the whole point of checkpoints: To be able to revert to them from anytime in the future. [cite: 51] [cite_start]They are file specific. [cite: 52]

11. [cite_start]It is mentioned that Reading, Writing, Streaming: The NM identifies the correct Storage Server and returns the precise IP address and client port for that SS to the client. [cite: 53] Subsequently, the client directly communicates with the designated SS. [cite_start]How exactly does NM identifies the correct SS for the client (in case there are multiple SS hosting the same file)? [cite: 54]

    > [cite_start]**[AG]** Any of the SS containing that file would be the "correct SS". [cite: 59] [cite_start]So, NM can return the IP and port of any of them, preferrably one with lower load if you can employ some mechanism to judge that. [cite: 60]

12. [cite_start]Lets consider File1 is in SS1 and File2 is in SS2. [cite: 61] Say direct connection between SS1 and client is established. [cite_start]Now if client wants to access File2 will a new connection establish between SS2 and client or will it just say file not exists? [cite: 62]

    > **[AG]** Client-SS relations are per request. [cite_start]So, after the client's File1 request is finished the Client-SS1 connection is terminated. [cite: 63] [cite_start]For the next request to File2 from SS2 (or even File1 from SS1), a fresh connection between the two need to be established. [cite: 64]

13. [cite_start]What exactly does it mean by sending a predefined STOP packet? [cite: 65] [cite_start]Does it mean that the user has to send a STOP packet explicitly, or after read, write or stream task is completed, it automatically terminates the connection? [cite: 66]

    > [cite_start]**[AG]** - A STOP packet signifies the end of the communication. [cite: 67] [cite_start]It must be explicitly sent for the receiver to realise the end of data / communication. [cite: 68]

14. [cite_start]Do we have to define each data blocks size and the number of data blocks in the storage? [cite: 69] [cite_start]Also, is there any restriction on the storage capacity of each storage server? [cite: 70]

    > [cite_start]**[AG]** - Depending on your implementation, you might have to. [cite: 71] [cite_start]There is no specific restriction, as in your system storage is the maximum storage capacity of the server. [cite: 72] [cite_start]But, you can expect the evaluation requirements to not go beyond a few MBs of data. [cite: 73]

15. [cite_start]In example 4, [cite: 74]
    ```
    Client: READ mouse.txt
    Im just a deeply mistaken hollow pocket-sized lil gei-fwen mouse. I dont like T
    Client: WRITE mouse.txt 1 # Inserting a sentence delimiter
    Client: 5 and AAD. aaaah # New sentences: [I dont like T-T PNS and AAD.]* [aaa
    Client: 0 But, # New sentence: [But, I dont like T-T PNS and AAD.]* [aaaah].
    Client: ETIRW
    Write Successful!
    Client: READ mouse.txt
    Im just a deeply mistaken hollow pocket-sized lil gei-fwen mouse. But, I dont l
    ```
    [cite_start][cite: 76, 77, 78, 79, 80, 81, 82, 83]
    [cite_start]Why has there been a delimiter added at the end of aaaah? [cite: 84]

    > [cite_start]**[AG]** - For a user, it is just a full stop, they can add it anywhere. [cite: 89] The system interprets it as a delimiter. [cite_start]So, no reasoning as to why the user chose to end his statement with a full-stop. [cite: 90]

16. [cite_start]Should we include LLM Generations with the files? [cite: 91]

    > [cite_start]**[AG]** Yes, just clearly demarkate it, and add appropriate links in a seperate README file. [cite: 92]

17. (Question regarding diagram from tutorial) [cite_start][cite: 102]
    [cite_start]this was shown in the tut online explanation, just wanted to make it clear [cite: 102]
    [cite_start]does it work like, lets say it was [cite: 103]
    ```
    [S1->hello->folks
    S2-> hows->life
    so on ..]
    ```
    [cite_start][cite: 104, 105, 106]
    [cite_start]I want to insert the following after hello [cite: 107]
    ```
    $S->hi$
    $S^{\prime}->!$
    $S^{\prime\prime}->!$
    ```
    [cite_start][cite: 108, 109, 110]
    [cite_start]so will the final be [cite: 111]
    ```
    Γ
    S1-> hello hi
    !
    ! folks
    ]
    ```
    [cite_start][cite: 112, 113, 114, 115, 116]
    [cite_start]is this correct? [cite: 120]

    > [cite_start]**[AG]** - Yes (it's just a demo implementation, you may choose to implement different data structure, etc) [cite: 122]

18. [cite_start]does backup start automatically when we connect one of the storage server, or does that also require explicit connection. [cite: 123] [cite_start]for eg: if I start storage server 1, does the backup for it start automatically, or I need to start its backup also myself? [cite: 124]

    > [cite_start]**[AG]** - As soon as a new 'empty' server comes up online, it can start working as a backup for some other server. [cite: 125] [cite_start]The decision wether it acts as a backup or not, depends on your implementation. [cite: 126] [cite_start]But you should ensure that if resources are available, all data has at least one copy for backup. [cite: 127]

19. [cite_start]For `READ`/`WRITE`/`STREAM` operations: After NM gives SS details to client, should the client open a new TCP socket to SS? [cite: 128] [cite_start]Should SS have two listening ports: one for NM, one for clients? [cite: 129]

    > **[AG]** - Yes, while maintaining its connection to NM. [cite_start]Yes [cite: 130]

20. [cite_start]How exactly is a user trying to write affected when there are $>=1$ readers or streamers? [cite: 131] [cite_start]For example, if the user is trying to commit his write while the file is being read/streamed. [cite: 132] Should this request be queued after the reads/streams? [cite_start]Or should the writer be given priority by interrupting the readers/streamers? [cite: 133]

    > [cite_start]**[AG]** - (Answered previously) Unless the `WRITE` is completed, all accesses to the file return the original data (before write). [cite: 134] [cite_start]Priority should be given to read/stream. [cite: 135]

21. [cite_start]Till what extent are we expected to handle packet loss? [cite: 136] [cite_start]Can we assume no "ACK"s will be lost or are we supposed to handle loss of ACKs as well? [cite: 137] [cite_start]Please clarify the extent of error handling for the networking side of things. [cite: 138]

    > **[AG]** The user should never exit abruptly. [cite_start]If there is a loss of packet, there should be mechanisms for retransmission and fall-backs. [cite: 139] Handle worst-case scenarios of whole network chain broken, and appropriate and graceful error handling. [cite_start]Some retransmission is expected. [cite: 140]

22. [cite_start]The 5 marks of bonus will be granted based on how we implement the project specifications or will they also be granted based on new/extra features we devise? [cite: 141]

    > **[AG]** - Assuming this doubt is for the "Unique Factor" part. [cite_start]New / extra feature. [cite: 142]

23. [cite_start]Undo Change: Users can rever the last changes made to a file. [cite: 147] [cite_start]does this mean that if i do `WRITE <some_sentences>` and then i do `ETIRW` and after that when i undo it removes ALL the sentences that were written by `WRITE`? [cite: 148] [cite_start]Also if i do undo again, should it restore the sentences or undo whatever change was made before that or do nothing? [cite: 149]

    > **[AG]** It undoes all the sentences within that single `WRITE` command. [cite_start]Undo again, is not redo. [cite: 150] [cite_start]It will undo whatever change was made before that. [cite: 151]

24. [cite_start]`Client: WRITE mouse.txt 1 # Inserting a sentence delimiter Client: 5 and AAD.` [cite: 152]
    [cite_start]`aaaah # New sentences [I dont like T-T PNS and AAD.]* [aaaah .` [cite: 153]
    [cite_start]`Currently active status remains with the index at index 1 Client: 0 But, # New sentence: [But, I dont like T-T PNS and AAD.]* [aaaah].` [cite: 154]
    [cite_start]Here, if I try the command: `client: 9 abcd` Should I get an error because I'm referencing next sentence or should it succeed? [cite: 155]

    > **[AG]** - 9 would give error. [cite_start]But 8 would append to the first sentence. [cite: 156] [cite_start]You cannot jump sentences by giving a large index. [cite: 157]

25. [cite_start]The SS sends a list of files on it when it registers. [cite: 158] [cite_start]Can we assume that 4096 bytes is enough for a very long list? [cite: 159]

    > [cite_start]**[AG]** No, we'll prefer a more robust way, like a stream of packets you send until the whole list is sent with an associated ACK bit of sorts to confirm the whole list is transmitted. [cite: 160]

26. [cite_start]Should we Load ALL files at server startup (slower startup, all files always ready)or do Lazy loading load file on first access, keep in memory once loaded (faster startup) what to do? [cite: 161]

    > **[AG]** No, you should never load all data files. [cite_start]Bad design when you have humongous data stores. [cite: 162] You should always load data on request ONLY. [cite_start]And ensure you keep clearing memory (cache) after some time. [cite: 163] [cite_start]The whole point of cache is to be small for faster access. [cite: 164]

27. [cite_start]Are the shell commands in the file to be `EXEC`'d singular commands? [cite: 165] [cite_start]Or can they include pipes, background &s etc? [cite: 166]

    > [cite_start]**[AG]** - Won't be singular commands, can include file read/writes, some computation, etc. [cite: 167, 172] (Ideally, you should not run such files on servers as is, very insecure)[cite_start]. [cite: 172] [cite_start]But, you can assume that these files would clear up all the data / files they create, not mess up any existing data and would not hog resources. [cite: 173]

28. [cite_start]If access is attempted to be given to a user that is not registered (does not show up in list), should we give an error? [cite: 174]

    > [cite_start]**[AG]** - Yes, an unregistered user should ideally not even get access to the interface. [cite: 175]

29. [cite_start]Is cjson header allowed to be used? [cite: 176]

    > [cite_start]**[AG]** - You may use any POSIX library. [cite: 177]

30. [cite_start]If a library is not posix, is it allowed to be used? [cite: 178]

    > [cite_start]**[AG]** No, only POSIX libraries are allowed. [cite: 179]

31. Following up on 27., they can be commands other than bash also? [cite_start]Please clarify 'shell commands'. [cite: 180] They need not be bash commands? [cite_start]So they can be `READ`, `WRITE`, `STREAM`, `CREATEFOLDER` etc.? [cite: 181]

    > [cite_start]**[AG]** - Only bash commands would be there [cite: 182]

32. [cite_start]For the `EXEC` operation, do we have to separately interpret the file and then send for execution? [cite: 183] [cite_start]For example, let a series of `WRITE` operations lead to the file `hi.txt` have `echo hiii. ls`. [cite: 184] [cite_start]Is the expected output: [cite: 185]
    ```
    hiii \n<list of files in directory>
    or hiii. \n<list of files in directory>
    or hiii. ls
    ```
    [cite_start][cite: 186]

    > [cite_start]**[AG]** Treat the whole file as a bash script. [cite: 187]

33. [cite_start]For `EXEC` operation, are we supposed to send request from client to ns, then fetch the file from ss and execute them on ns itself? [cite: 188] [cite_start]So this flow has to be different from the normal `READ` and `WRITE` operations right? [cite: 189] And should the whole file be treated as a bash script? [cite_start]Or we have to parse it sentence by sentence? [cite: 190]

    > **[AG]** Yes, the flow would be different from `READ`/`WRITE`. [cite_start]Yes, treat it as a bash script, dont parse it sentence-by-sentence. [cite: 191]

34. [cite_start]Is the undo command supposed to store only one level of history, or is the code supposed to support multiple simultaneous undos chained together? [cite: 196]

    > [cite_start]**[AG]** Just one-level works, we only need a proof of concept [cite: 197]

35. When we are implementing `WRITE`, do we add a newline after adding each sentence? [cite_start]According to example 2, yes. [cite: 198] According to example 4, no. [cite_start]So what do we do? [cite: 199]

    > [cite_start]**[AG]** The content of the file would have '\n' to signify new line. [cite: 200] [cite_start]Both examples follow that, just example 4 never encountered a '\n'. [cite: 201]

36. (Question about `LIST` command and user registration) [cite_start][cite: 202]...run `LIST` from the bob server. [cite_start]Should the output be: [cite: 203]
    ```
    ->alice
    ->bob
    ```
    [cite_start][cite: 204, 205]
    [cite_start]since alice was technically registered before? or does the output change to: [cite: 206]
    ```
    ->bob
    ```
    [cite_start][cite: 207]

    > **[AG]** - Registered users include users not currently online. [cite_start]You must store all users that have logged in till date. [cite: 208]

37. [cite_start]In the document it is mentioned "At any point, there would be a single instance of the Name Server running, to which multiple instances of Storage Servers and User Clients can connect. The User Clients and Storage Servers can disconnect and reconnect at any time, and the system should handle these events gracefully." [cite: 209] [cite_start]My question - Does NM need to be able to disconnect using ctrl+c? [cite: 210] [cite_start]or is stopping it using ctrl+z fine, as the exiting of this server is not mentioned in the project document? [cite: 211]

    > [cite_start]**[AG]** No, you can assume that once NM starts, it stays on for the whole duration. [cite: 212] [cite_start]NM going down is not in the scope of this project. [cite: 213]

38. [cite_start]If the owner is attempting to delete a file, but the file is being written to by another user, are we expected to: [cite: 214]
    [cite_start]a) Prevent the owner from deleting with an error message? [cite: 215]
    [cite_start]b) Give an error message to the editing user? [cite: 216]

    > [cite_start]**[AG]** - Prevent deletion as the sentence is locked. [cite: 217]

39. [cite_start]In the 23rd answer you have mentioned that multiple undoes are supported for one file while the project doc explicitly states they are not(under example 5)?! [cite: 222]

    > **[AG]** The minimum requirement is one-level undo. [cite_start]People might choose to expand features or develop their unique things for the bonus. [cite: 223]

40. [cite_start]For performing `UNDO`, does a user need to have write permission for that file? [cite: 224]
    > [cite_start]**[AG]** - Yes [cite: 224]

41. What is expected to be done in caching? [cite_start]Does it need to be implemented on the ns or ss? [cite: 225] [cite_start]As on the ns, it would just involve caching the storage server id for the corresponding file, whose access is already $O(1)$ or O(log n) average case. [cite: 226, 227]

    > [cite_start]**[AG]** It won't be $O(1)$, might tend to but not $O(1)$ But the cached mappings should be $O(1)$ always. [cite: 228]

42. [cite_start]can we use glibc? [cite: 229]

    > [cite_start]**[AG]** - You may use any (and only) POSIC libraries. [cite: 230]

43. [cite_start]do access lookups also need to be fast? [cite: 231]

    > **[AG]** Yes, they need to be sub-linear time complexity. [cite_start]And cached ones need to be constant time. [cite: 232]

44. [cite_start]If a user is writing to a file and has a sentence locked, but the client disconnects midway, should other users be allowed to write to that sentence? [cite: 233] [cite_start]Do we leave the sentence as locked until the previously disconnected user writes that sentence again? [cite: 234]

    > [cite_start]**[AG]** If the client disconnects midway a `WRITE` (ie, before sending the `ERITW`), no changes of that write should pass. [cite: 235] It's as if the write never happened. [cite_start]And all locks are relieved for other users to access the file without any issues. [cite: 236]