/*
  
  Adapted as a Generic Data structure with Template C++ by AG
  

           An implementation of top-down splaying with sizes
             D. Sleator <sleator@cs.cmu.edu>, January 1994.

  This extends top-down-splay.c to maintain a size field in each node.
  This is the number of nodes in the subtree rooted there.  This makes
  it possible to efficiently compute the rank of a key.  (The rank is
  the number of nodes to the left of the given key.)  It it also
  possible to quickly find the node of a given rank.  Both of these
  operations are illustrated in the code below.  The remainder of this
  introduction is taken from top-down-splay.c.

  "Splay trees", or "self-adjusting search trees" are a simple and
  efficient data structure for storing an ordered set.  The data
  structure consists of a binary tree, with no additional fields.  It
  allows searching, insertion, deletion, deletemin, deletemax,
  splitting, joining, and many other operations, all with amortized
  logarithmic performance.  Since the trees adapt to the sequence of
  requests, their performance on real access patterns is typically even
  better.  Splay trees are described in a number of texts and papers
  [1,2,3,4].

  The code here is adapted from simple top-down splay, at the bottom of
  page 669 of [2].  It can be obtained via anonymous ftp from
  spade.pc.cs.cmu.edu in directory /usr/sleator/public.

  The chief modification here is that the splay operation works even if the
  item being splayed is not in the tree, and even if the tree root of the
  tree is NULL.  So the line:

                              t = splay(i, t);

  causes it to search for item with key i in the tree rooted at t.  If it's
  there, it is splayed to the root.  If it isn't there, then the node put
  at the root is the last one before NULL that would have been reached in a
  normal binary search for i.  (It's a neighbor of i in the tree.)  This
  allows many other operations to be easily implemented, as shown below.

  [1] "Data Structures and Their Algorithms", Lewis and Denenberg,
       Harper Collins, 1991, pp 243-251.
  [2] "Self-adjusting Binary Search Trees" Sleator and Tarjan,
       JACM Volume 32, No 3, July 1985, pp 652-686.
  [3] "Data Structure and Algorithm Analysis", Mark Weiss,
       Benjamin Cummins, 1992, pp 119-  130.
  [4] "Data Structures, Algorithms, and Performance", Derick Wood,
       Addison-Wesley, 1993, pp 367-375
*/

#include <iostream>
#include <set>
#include <boost/dynamic_bitset.hpp>

// Needed to use report_fatal_error
using namespace boost;
using namespace std;





//
// Removed: for generic T we may have defined >, <, == but not -
//
//#define compare(i,j) ((i)-(j))
///* This is the comparison.                                       */
///* Returns <0 if i<j, =0 if i=j, and >0 if i>j                   */

//
// Remove: made it generic and inline
//
//#define node_size(x) (((x)==NULL) ? 0 : ((x)->size))
///* This macro returns the size of a node.  Unlike "x->size",     */
///* it works even if x=NULL.  The test could be avoided by using  */
///* a special version of NULL which was a real node with size 0.  */

namespace SplayTree{

  template <typename T> 
  struct Tree{
    Tree(){left = NULL; right = NULL; prev = NULL;size=1; /*capacity=1;last_record=1*/; issueOccupancy = 0; widthOccupancy = 0;occupancyPrefetch = 0;}

    Tree * left, * right, *prev;
    T key;
    size_t size;   /* maintained to be the number of nodes rooted here */
    int32_t issueOccupancy;
    int32_t widthOccupancy;
    int32_t occupancyPrefetch;
    uint64_t address;
  };

  
  ///////////////////////// FREE METHODS

  template <typename T>
  inline size_t node_size(Tree<T>* x){ return (x==NULL) ? 0 : x->size; }
  

  // Splay using the key i (which may or may not be in the tree.) 
  // The starting root is t, and the tree used is defined by rat  
  // size fields are maintained */
  //
  template <typename T>
  Tree<T> * splay (T i, Tree<T> *t) 
  {
    Tree<T> N, *l, *r, *y;
    size_t root_size, l_size, r_size;
    
    if (t == NULL) return t;
    N.left = N.right = NULL;
    l = r = &N;
    root_size = node_size(t);
    l_size = r_size = 0;
 
    for (;;) {

      //      comp = compare(i, t->key);
      //if (comp < 0) {
      if (i < t->key) {
    if (t->left == NULL) break;

    //  if (compare(i, t->left->key) < 0) {
    if (i < t->left->key) {
      y = t->left;                           /* rotate right */
      t->left = y->right;
      y->right = t;
      t->size = node_size(t->left) + node_size(t->right) + 1;
      t = y;
      if (t->left == NULL) break;
    }
    r->left = t;                               /* link right */
    r = t;
    t = t->left;
    r_size += 1+node_size(r->right);
      } else if (i > t->key) {
    if (t->right == NULL) break;

    //  if (compare(i, t->right->key) > 0) {
    if (i > t->right->key) {
      y = t->right;                          /* rotate left */
      t->right = y->left;
      y->left = t;
      t->size = node_size(t->left) + node_size(t->right) + 1;
      t = y;
      if (t->right == NULL) break;
    }
    l->right = t;                              /* link left */
    l = t;
    t = t->right;
    l_size += 1+node_size(l->left);
      } else {
    break;
      }
    }
    l_size += node_size(t->left);  /* Now l_size and r_size are the sizes of */
    r_size += node_size(t->right); /* the left and right trees we just built.*/
    t->size = l_size + r_size + 1;

    l->right = r->left = NULL;

    /* The following two loops correct the size fields of the right path  */
    /* from the left child of the root and the right path from the left   */
    /* child of the root.                                                 */
    for (y = N.right; y != NULL; y = y->right) {
      y->size = l_size;
      l_size -= 1+node_size(y->left);
    }
    for (y = N.left; y != NULL; y = y->left) {
      y->size = r_size;
      r_size -= 1+node_size(y->right);
    }
 
    l->right = t->left;                                /* assemble */
    r->left = t->right;
    t->left = N.right;
    t->right = N.left;

    return t;
  }

  // Insert key i into the tree t, if it is not already there. 
  // Return a pointer to the resulting tree.                   
  //
  template <typename T>
  Tree<T> * insert_node(T i, Tree<T> * t, uint64_t a = 0) {

    
    Tree<T> * new_node;

    if (t != NULL) {

      t = splay(i,t);
      //if (compare(i, t->key)==0) {
      if (i == t->key){
        return t;  /* it's already there */
      }
    }
    //new_node = (Tree *) malloc (sizeof (Tree));
    //    if (new_node == NULL) {printf("Ran out of space\n"); exit(1);}
    new_node = new Tree<T>();
    if(new_node == NULL)
       std::cout << "Object could not be allocated!\n";
    
    
    if (t == NULL) {
      
      new_node->left = new_node->right = NULL;

      //    } else if (compare(i, t->key) < 0) {
    } else if (i < t->key) {
      new_node->left = t->left;
      new_node->right = t;
      t->left = NULL;
      t->size = 1+node_size(t->right);
    } else {
      new_node->right = t->right;
      new_node->left = t;
      t->right = NULL;
      t->size = 1+node_size(t->left);
    }
    new_node->address = a;

    new_node->key = i;
    new_node->size = 1 + node_size(new_node->left) + node_size(new_node->right);
    //New code
  //  new_node->occupacy = occupacy;
    return new_node;
  }

  
  
  
  // Deletes i from the tree if it's there.              
  // Return a pointer to the resulting tree.              
  //
  template <typename T>
  Tree<T> * delete_node(T i, Tree<T> *t) {
    Tree<T> * x;
    size_t tsize;

    if (t==NULL) return NULL;
    tsize = t->size;
    t = splay(i,t);

    //if (compare(i, t->key) == 0) {               /* found it */
    if (i == t->key) {               /* found it */
      if (t->left == NULL) {
    x = t->right;
      } else {
    x = splay(i, t->left);
    x->right = t->right;
      }
      //free(t);
      delete t;
      if (x != NULL) {
    x->size = tsize-1;

      }
      return x;
    } else {
      return t;                         /* It wasn't there */
    }
  }

  

  //Returns a pointer to the node in the tree with the given rank.  
  // Returns NULL if there is no such node.                          
  //  Does not change the tree.  To guarantee logarithmic behavior,  
  // the node found here should be splayed to the root.              
  //
  template <typename T>
  Tree<T> *find_rank(int r, Tree<T> *t) { 
    int lsize;
    if ((r < 0) || (r >= (int) node_size(t))) return NULL;
    for (;;) {
      lsize = node_size(t->left);
      if (r < lsize) {
    t = t->left;
      } else if (r > lsize) {
    r = r - lsize -1;
    t = t->right;
      } else {
    return t;
      }
    }
  }

 
  
  
  template <typename T>
  bool find_key(T i, Tree<T> *t ) {

    if (t != NULL) {
    t = splay(i,t);
    //if (compare(i, t->key) == 0) {               // found it
    std::cout << "i "<< i<<std::endl;
      if (i == t->key) {// found it

      return true;
    }else
      return false;
    }
    else{
      return false;
  
    }
  }

  template <typename T>
  int tree_size(Tree<T> * t) {
    if (t==NULL) {
      return(0);
    }
    return( tree_size(t->left) + tree_size(t->right) + 1 );
  }
  
  
  // print the tree
  //
  template <typename T>
  void printtree(Tree<T> * t, unsigned int d) {
    if (t == NULL) return;
    printtree(t->right, d+1);
    for (unsigned int i=0; i<d; i++) std::cout << "  ";
    std::cout << t->key <<"("<< t->size<<")"<<std::endl;
    printtree(t->left, d+1);
  }
  
  template <typename T>
  void delete_all (Tree<T> * t )
  {
    if ( t != NULL ) {
      //std::cerr << "Deleting subtree with key: " << t->key << ", size: " << t->size << "\n" ;
      if (t->size == 1) {
        delete t;
        //std::cerr << "Done deleting subtree\n";
        return;
      }
      if (t->left != NULL && t->left->size < t->size) {
        //std::cerr << "Boo left\n";
        delete_all ( t->left );
      }
      if (t->right != NULL && t->right->size < t->size) {
        //std::cerr << "Boo right\n";
        delete_all ( t->right );
      }
    }
  }
  
}
 





namespace SplayTreeBoolean{

  
  template <typename T>
  struct TreeBitVector{
     TreeBitVector():BitVector(24){  // MAX_RESOURCE_VALUE
      left = NULL; right = NULL; prev = NULL;size=1; /*capacity=1;last_record=1;*/
      
    }
    
    TreeBitVector * left, * right, *prev;
    T key;
    size_t size;   /* maintained to be the number of nodes rooted here */

    dynamic_bitset<> BitVector; // all 0's by default
  };
  
  
  

  
  
  ///////////////////////// FREE METHODS
  
  template <typename T>
  inline size_t node_size( TreeBitVector<T>* x){ return (x==NULL) ? 0 : x->size; }
  
  
  // Splay using the key i (which may or may not be in the tree.)
  // The starting root is t, and the tree used is defined by rat
  // size fields are maintained */
  //
  template <typename T>
   TreeBitVector<T> * splay (T i,  TreeBitVector<T> *t)
  {
     TreeBitVector<T> N, *l, *r, *y;
    size_t root_size, l_size, r_size;
    
    if (t == NULL) return t;
    N.left = N.right = NULL;
    l = r = &N;
    root_size = node_size(t);
    l_size = r_size = 0;
    
    for (;;) {
      
      //      comp = compare(i, t->key);
      //if (comp < 0) {
      if (i < t->key) {
        if (t->left == NULL) break;
        
        //  if (compare(i, t->left->key) < 0) {
        if (i < t->left->key) {
          y = t->left;                           /* rotate right */
          t->left = y->right;
          y->right = t;
          t->size = node_size(t->left) + node_size(t->right) + 1;
          t = y;
          if (t->left == NULL) break;
        }
        r->left = t;                               /* link right */
        r = t;
        t = t->left;
        r_size += 1+node_size(r->right);
      } else if (i > t->key) {
        if (t->right == NULL) break;
        
        //  if (compare(i, t->right->key) > 0) {
        if (i > t->right->key) {
          y = t->right;                          /* rotate left */
          t->right = y->left;
          y->left = t;
          t->size = node_size(t->left) + node_size(t->right) + 1;
          t = y;
          if (t->right == NULL) break;
        }
        l->right = t;                              /* link left */
        l = t;
        t = t->right;
        l_size += 1+node_size(l->left);
      } else {
        break;
      }
    }
    l_size += node_size(t->left);  /* Now l_size and r_size are the sizes of */
    r_size += node_size(t->right); /* the left and right trees we just built.*/
    t->size = l_size + r_size + 1;
    
    l->right = r->left = NULL;
    
    /* The following two loops correct the size fields of the right path  */
    /* from the left child of the root and the right path from the left   */
    /* child of the root.                                                 */
    for (y = N.right; y != NULL; y = y->right) {
      y->size = l_size;
      l_size -= 1+node_size(y->left);
    }
    for (y = N.left; y != NULL; y = y->left) {
      y->size = r_size;
      r_size -= 1+node_size(y->right);
    }
    
    l->right = t->left;                                /* assemble */
    r->left = t->right;
    t->left = N.right;
    t->right = N.left;
    
    return t;
  }
  
  // Insert key i into the tree t, if it is not already there.
  // Return a pointer to the resulting tree.
  //
  template <typename T>
  TreeBitVector<T> * insert_node(T i,  unsigned bitPosition, TreeBitVector<T> * t) {
    
    
     TreeBitVector<T> * new_node;
    
    if (t != NULL) {
      t = splay(i,t);
      //if (compare(i, t->key)==0) {
      if (i == t->key){
        t->BitVector[bitPosition]=1;
        return t;  /* it's already there */
      }
    }
    //new_node = (Tree *) malloc (sizeof (Tree));
    //    if (new_node == NULL) {printf("Ran out of space\n"); exit(1);}
    new_node = new  TreeBitVector<T>();
    if(new_node == NULL)
      std::cout << "Object could not be allocated!\n";
    
    
    new_node->BitVector[bitPosition] = 1;
    if (t == NULL) {
      new_node->left = new_node->right = NULL;
      
      //    } else if (compare(i, t->key) < 0) {
    } else if (i < t->key) {
      new_node->left = t->left;
      new_node->right = t;
      t->left = NULL;
      t->size = 1+node_size(t->right);
    } else {
      new_node->right = t->right;
      new_node->left = t;
      t->right = NULL;
      t->size = 1+node_size(t->left);
    }
    new_node->key = i;
    new_node->size = 1 + node_size(new_node->left) + node_size(new_node->right);
    //New code
    //  new_node->occupacy = occupacy;
    return new_node;
  }
  
  
  
  
  // Deletes i from the tree if it's there.
  // Return a pointer to the resulting tree.
  //
  template <typename T>
   TreeBitVector<T> * delete_node(T i, TreeBitVector<T> *t) {
     TreeBitVector<T> * x;
    size_t tsize;
    
    if (t==NULL) return NULL;
    tsize = t->size;
    t = splay(i,t);
    //if (compare(i, t->key) == 0) {               /* found it */
    if (i == t->key) {               /* found it */
      if (t->left == NULL) {
        x = t->right;
      } else {
        x = splay(i, t->left);
        x->right = t->right;
      }
      //free(t);
      delete t;
      if (x != NULL) {
        x->size = tsize-1;
        
      }
      return x;
    } else {
      return t;                         /* It wasn't there */
    }
  }
  
  
  //Returns a pointer to the node in the tree with the given rank.
  // Returns NULL if there is no such node.
  //  Does not change the tree.  To guarantee logarithmic behavior,
  // the node found here should be splayed to the root.
  //
  template <typename T>
   TreeBitVector<T> *find_rank(int r, TreeBitVector<T> *t) {
    int lsize;
    if ((r < 0) || (r >= (int) node_size(t))) return NULL;
    for (;;) {
      lsize = node_size(t->left);
      if (r < lsize) {
        t = t->left;
      } else if (r > lsize) {
        r = r - lsize -1;
        t = t->right;
      } else {
        return t;
      }
    }
  }
  
  
  
  
  template <typename T>
  bool find_key(T i,  TreeBitVector<T> *t ) {
    
    if (t != NULL) {
      t = splay(i,t);
      //if (compare(i, t->key) == 0) {               // found it
      std::cout << "i "<< i<<std::endl;
      if (i == t->key) {// found it
        
        return true;
      }else
        return false;
    }
    else{
      return false;
      
    }
  }
  
  template <typename T>
  int tree_size( TreeBitVector<T> * t) {
    if (t==NULL) {
      return(0);
    }
    return( tree_size(t->left) + tree_size(t->right) + 1 );
  }
  
  
  // print the tree
  //
  template <typename T>
  void printtree( TreeBitVector<T> * t, unsigned int d) {
    if (t == NULL) return;
    printtree(t->right, d+1);
    for (unsigned int i=0; i<d; i++) std::cout << "  ";
    std::cout << t->key <<"("<< t->size<<")"<<std::endl;
    printtree(t->left, d+1);
  }
  
  template <typename T>
   TreeBitVector<T> * delete_all ( TreeBitVector<T> * t )
  {
    if ( t != NULL ) {
      t = delete_all ( t->left );
      t = delete_all ( t->right );
      delete t;
      t = NULL;
      return t;
      
    }
    
  }
  
}




 

namespace SplayTreeVector{

  
  template <typename T>
  struct TreeVector{
    TreeVector(){
      left = NULL;
      right = NULL;
      prev = NULL;
      size=1; /*capacity=1;last_record=1;*/
      for(int i =0; i< 24;i++) // MAX_RESOURCE_VALUE
        ResourcesWidthOccuppancy.push_back(0);
      ;
    }
    
    TreeVector * left, * right, *prev;
    T key;
    size_t size;   /* maintained to be the number of nodes rooted here */
    //  int capacity;
   // int last_record;
    std::vector<unsigned> ResourcesWidthOccuppancy;
    
  };
  
  
  ///////////////////////// FREE METHODS
  
  template <typename T>
  inline size_t node_size(TreeVector<T>* x){ return (x==NULL) ? 0 : x->size; }
  
  
  // Splay using the key i (which may or may not be in the tree.)
  // The starting root is t, and the tree used is defined by rat
  // size fields are maintained */
  //
  template <typename T>
  TreeVector<T> * splay (T i, TreeVector<T> *t)
  {
    TreeVector<T> N, *l, *r, *y;
    size_t root_size, l_size, r_size;
    
    if (t == NULL) return t;
    N.left = N.right = NULL;
    l = r = &N;
    root_size = node_size(t);
    l_size = r_size = 0;
    
    for (;;) {
      
      //      comp = compare(i, t->key);
      //if (comp < 0) {
      if (i < t->key) {
        if (t->left == NULL) break;
        
        //  if (compare(i, t->left->key) < 0) {
        if (i < t->left->key) {
          y = t->left;                           /* rotate right */
          t->left = y->right;
          y->right = t;
          t->size = node_size(t->left) + node_size(t->right) + 1;
          t = y;
          if (t->left == NULL) break;
        }
        r->left = t;                               /* link right */
        r = t;
        t = t->left;
        r_size += 1+node_size(r->right);
      } else if (i > t->key) {
        if (t->right == NULL) break;
        
        //  if (compare(i, t->right->key) > 0) {
        if (i > t->right->key) {
          y = t->right;                          /* rotate left */
          t->right = y->left;
          y->left = t;
          t->size = node_size(t->left) + node_size(t->right) + 1;
          t = y;
          if (t->right == NULL) break;
        }
        l->right = t;                              /* link left */
        l = t;
        t = t->right;
        l_size += 1+node_size(l->left);
      } else {
        break;
      }
    }
    l_size += node_size(t->left);  /* Now l_size and r_size are the sizes of */
    r_size += node_size(t->right); /* the left and right trees we just built.*/
    t->size = l_size + r_size + 1;
    
    l->right = r->left = NULL;
    
    /* The following two loops correct the size fields of the right path  */
    /* from the left child of the root and the right path from the left   */
    /* child of the root.                                                 */
    for (y = N.right; y != NULL; y = y->right) {
      y->size = l_size;
      l_size -= 1+node_size(y->left);
    }
    for (y = N.left; y != NULL; y = y->left) {
      y->size = r_size;
      r_size -= 1+node_size(y->right);
    }
    
    l->right = t->left;                                /* assemble */
    r->left = t->right;
    t->left = N.right;
    t->right = N.left;
    
    return t;
  }
  
  // Insert key i into the tree t, if it is not already there.
  // Return a pointer to the resulting tree.
  //
  template <typename T>
  TreeVector<T> * insert_node(T i, TreeVector<T> * t) {
    
    
    
    TreeVector<T> * new_node;
    
    if (t != NULL) {
      t = splay(i,t);
      //if (compare(i, t->key)==0) {
      if (i == t->key){
        return t;  /* it's already there */
      }
    }
    //new_node = (Tree *) malloc (sizeof (Tree));
    //    if (new_node == NULL) {printf("Ran out of space\n"); exit(1);}
    new_node = new TreeVector<T>();
    if(new_node == NULL)
      std::cout << "Object could not be allocated!\n";
    
    
    if (t == NULL) {
      new_node->left = new_node->right = NULL;
      
      //    } else if (compare(i, t->key) < 0) {
    } else if (i < t->key) {
      new_node->left = t->left;
      new_node->right = t;
      t->left = NULL;
      t->size = 1+node_size(t->right);
    } else {
      new_node->right = t->right;
      new_node->left = t;
      t->right = NULL;
      t->size = 1+node_size(t->left);
    }
    new_node->key = i;
    new_node->size = 1 + node_size(new_node->left) + node_size(new_node->right);
    //New code
    //  new_node->occupacy = occupacy;
    return new_node;
  }
  
  
  
  
  // Deletes i from the tree if it's there.
  // Return a pointer to the resulting tree.
  //
  template <typename T>
  TreeVector<T> * delete_node(T i, TreeVector<T> *t) {
    TreeVector<T> * x;
    size_t tsize;
    
    if (t==NULL) {
      return NULL;}
    
    tsize = t->size;
    t = splay(i,t);
    //if (compare(i, t->key) == 0) {               /* found it */
    if (i == t->key) {               /* found it */
      if (t->left == NULL) {
        x = t->right;
      } else {
        x = splay(i, t->left);
        x->right = t->right;
      }
      //free(t);
      delete t;
      if (x != NULL) {
        x->size = tsize-1;
        
      }
      return x;
    } else {
      return t;                         /* It wasn't there */
    }
  }
  
  
  //Returns a pointer to the node in the tree with the given rank.
  // Returns NULL if there is no such node.
  //  Does not change the tree.  To guarantee logarithmic behavior,
  // the node found here should be splayed to the root.
  //
  template <typename T>
  TreeVector<T> *find_rank(int r, TreeVector<T> *t) {
    int lsize;
    if ((r < 0) || (r >= (int) node_size(t))) return NULL;
    for (;;) {
      lsize = node_size(t->left);
      if (r < lsize) {
        t = t->left;
      } else if (r > lsize) {
        r = r - lsize -1;
        t = t->right;
      } else {
        return t;
      }
    }
  }
  
  
  
  
  template <typename T>
  bool find_key(T i, TreeVector<T> *t ) {
    
    if (t != NULL) {
      t = splay(i,t);
      //if (compare(i, t->key) == 0) {               // found it
      std::cout << "i "<< i<<std::endl;
      if (i == t->key) {// found it
        
        return true;
      }else
        return false;
    }
    else{
      return false;
      
    }
  }
  
  template <typename T>
  int tree_size(TreeVector<T> * t) {
    if (t==NULL) {
      return(0);
    }
    return( tree_size(t->left) + tree_size(t->right) + 1 );
  }
  
  
  // print the tree
  //
  template <typename T>
  void printtree(TreeVector<T> * t, unsigned int d) {
    if (t == NULL) return;
    printtree(t->right, d+1);
    for (unsigned int i=0; i<d; i++) std::cout << "  ";
    std::cout << t->key <<"("<< t->size<<")"<<std::endl;
    printtree(t->left, d+1);
  }
  
  template <typename T>
  TreeVector<T> * delete_all (TreeVector<T> * t )
  {
    if ( t != NULL ) {
      t = delete_all ( t->left );
      t = delete_all ( t->right );
      delete t;
      t = NULL;
      return t;
      
    }
    
  }
  
}


namespace SimpleSplayTree {
  
  
  template <typename T>
  struct SimpleTree{
    SimpleTree(){left = NULL; right = NULL; prev = NULL;size=1;}
    
    SimpleTree * left, * right, *prev;
    T key;
    size_t size;   /* maintained to be the number of nodes rooted here */
    
  };
  
  
  ///////////////////////// FREE METHODS
  
  template <typename T>
  inline size_t node_size(SimpleTree<T>* x){ return (x==NULL) ? 0 : x->size; }
  
  
  // Splay using the key i (which may or may not be in the tree.)
  // The starting root is t, and the tree used is defined by rat
  // size fields are maintained */
  //
  template <typename T>
  SimpleTree<T> * splay (T i, SimpleTree<T> *t)
  {
    SimpleTree<T> N, *l, *r, *y;
    size_t root_size, l_size, r_size;
    
    if (t == NULL) return t;
    N.left = N.right = NULL;
    l = r = &N;
    root_size = node_size(t);
    l_size = r_size = 0;
    
    for (;;) {
      
      //      comp = compare(i, t->key);
      //if (comp < 0) {
      if (i < t->key) {
        if (t->left == NULL) break;
        
        //  if (compare(i, t->left->key) < 0) {
        if (i < t->left->key) {
          y = t->left;                           /* rotate right */
          t->left = y->right;
          y->right = t;
          t->size = node_size(t->left) + node_size(t->right) + 1;
          t = y;
          if (t->left == NULL) break;
        }
        r->left = t;                               /* link right */
        r = t;
        t = t->left;
        r_size += 1+node_size(r->right);
      } else if (i > t->key) {
        if (t->right == NULL) break;
        
        //  if (compare(i, t->right->key) > 0) {
        if (i > t->right->key) {
          y = t->right;                          /* rotate left */
          t->right = y->left;
          y->left = t;
          t->size = node_size(t->left) + node_size(t->right) + 1;
          t = y;
          if (t->right == NULL) break;
        }
        l->right = t;                              /* link left */
        l = t;
        t = t->right;
        l_size += 1+node_size(l->left);
      } else {
        break;
      }
    }
    l_size += node_size(t->left);  /* Now l_size and r_size are the sizes of */
    r_size += node_size(t->right); /* the left and right trees we just built.*/
    t->size = l_size + r_size + 1;
    
    l->right = r->left = NULL;
    
    /* The following two loops correct the size fields of the right path  */
    /* from the left child of the root and the right path from the left   */
    /* child of the root.                                                 */
    for (y = N.right; y != NULL; y = y->right) {
      y->size = l_size;
      l_size -= 1+node_size(y->left);
    }
    for (y = N.left; y != NULL; y = y->left) {
      y->size = r_size;
      r_size -= 1+node_size(y->right);
    }
    
    l->right = t->left;                                /* assemble */
    r->left = t->right;
    t->left = N.right;
    t->right = N.left;
    
    return t;
  }
  
  // Insert key i into the tree t, if it is not already there.
  // Return a pointer to the resulting tree.
  //
  template <typename T>
  SimpleTree<T> * insert_node(T i, SimpleTree<T> * t) {
    
    
    SimpleTree<T> * new_node;
    
    if (t != NULL) {
      
      t = splay(i,t);
      //if (compare(i, t->key)==0) {
      /* if (i == t->key){
       return t;  // it's already there
       }*/
    }
    //new_node = (SimpleTree *) malloc (sizeof (SimpleTree));
    //    if (new_node == NULL) {printf("Ran out of space\n"); exit(1);}
    new_node = new SimpleTree<T>();
    if(new_node == NULL)
      std::cout << "Object could not be allocated!\n";
    
    
    if (t == NULL) {
      
      new_node->left = new_node->right = NULL;
      
      //    } else if (compare(i, t->key) < 0) {
    } else if (i < t->key) {
      new_node->left = t->left;
      new_node->right = t;
      t->left = NULL;
      t->size = 1+node_size(t->right);
    } else {
      new_node->right = t->right;
      new_node->left = t;
      t->right = NULL;
      t->size = 1+node_size(t->left);
    }
    
    new_node->key = i;
    new_node->size = 1 + node_size(new_node->left) + node_size(new_node->right);
    //New code
    //  new_node->occupacy = occupacy;
    return new_node;
  }
  
  
  
  
  // Deletes i from the SimpleTree if it's there.
  // Return a pointer to the resulting SimpleTree.
  //
  template <typename T>
  SimpleTree<T> * delete_node(T i, SimpleTree<T> *t) {
    SimpleTree<T> * x;
    size_t tsize;
    
    if (t==NULL) return NULL;
    tsize = t->size;
    t = splay(i,t);
    
    //if (compare(i, t->key) == 0) {               /* found it */
    if (i == t->key) {               /* found it */
      if (t->left == NULL) {
        x = t->right;
      } else {
        x = splay(i, t->left);
        x->right = t->right;
      }
      //free(t);
      delete t;
      if (x != NULL) {
        x->size = tsize-1;
        
      }
      return x;
    } else {
      return t;                         /* It wasn't there */
    }
  }
  
  
  
  //Returns a pointer to the node in the tree with the given rank.
  // Returns NULL if there is no such node.
  //  Does not change the tree.  To guarantee logarithmic behavior,
  // the node found here should be splayed to the root.
  //
  template <typename T>
  SimpleTree<T> *find_rank(int r, SimpleTree<T> *t) {
    int lsize;
    if ((r < 0) || (r >= (int) node_size(t))) return NULL;
    for (;;) {
      lsize = node_size(t->left);
      if (r < lsize) {
        t = t->left;
      } else if (r > lsize) {
        r = r - lsize -1;
        t = t->right;
      } else {
        return t;
      }
    }
  }
  
  
  
  
  template <typename T>
  bool find_key(T i, SimpleTree<T> *t ) {
    
    if (t != NULL) {
      t = splay(i,t);
      //if (compare(i, t->key) == 0) {               // found it
      std::cout << "i "<< i<<std::endl;
      if (i == t->key) {// found it
        
        return true;
      }else
        return false;
    }
    else{
      return false;
      
    }
  }
  
  template <typename T>
  int tree_size(SimpleTree<T> * t) {
    if (t==NULL) {
      return(0);
    }
    return( tree_size(t->left) + tree_size(t->right) + 1 );
  }
  
  
   template <typename T>
  void printPostOrder(SimpleTree<T> * p)
  {
    if(p != NULL)
    {
      if(p->left) printPostOrder(p->left);
      if(p->right) printPostOrder(p->right);
      std::cout <<" "<<p->key<<" ";
    }
    else return;
  }
  
  
  
  // print the tree
  //
  template <typename T>
  void printtree(SimpleTree<T> * t, unsigned int d) {
    if (t == NULL) return;
    printtree(t->right, d+1);
    for (unsigned int i=0; i<d; i++) std::cout << "  ";
    std::cout << t->key <<"("<< t->size<<")"<<std::endl;
    printtree(t->left, d+1);
  }
  
  template <typename T>
  void delete_all (SimpleTree<T> * t )
  {
    if ( t != NULL ) {
      if (t->size == 1) {
        delete t;
        return;
      }
      if (t->left != NULL && t->left->size < t->size) {
        delete_all(t->left);
      }
      if (t->right != NULL && t->right->size < t->size) {
        delete_all(t->right);
      }
    }
  }
  
  template <typename T>
  T min(SimpleTree<T> * node) {
    T minimum = 0;
    
    if(node->left == NULL){
      return node->key;
    }
    minimum = min(node->left);
    return minimum;
    //return minHelper(node);
  }
  
  
}



namespace ComplexSplayTree {
  
  
  template <typename T>
  struct ComplexTree{
    ComplexTree(){left = NULL; right = NULL; prev = NULL;size=1;}
    
    ComplexTree * left, * right, *prev;
    T key; // The key is CompletionCycle
    T IssueCycle;
    size_t size;   /* maintained to be the number of nodes rooted here */
    
  };
  
  
  ///////////////////////// FREE METHODS
  
  template <typename T>
  inline size_t node_size(ComplexTree<T>* x){ return (x==NULL) ? 0 : x->size; }
  
  
  // Splay using the key i (which may or may not be in the tree.)
  // The starting root is t, and the tree used is defined by rat
  // size fields are maintained */
  //
  template <typename T>
  ComplexTree<T> * splay (T i, ComplexTree<T> *t)
  {
    ComplexTree<T> N, *l, *r, *y;
    size_t root_size, l_size, r_size;
    
    if (t == NULL) return t;
    N.left = N.right = NULL;
    l = r = &N;
    root_size = node_size(t);
    l_size = r_size = 0;
    
    for (;;) {
      
      //      comp = compare(i, t->key);
      //if (comp < 0) {
      if (i < t->key) {
        if (t->left == NULL) break;
        
        //  if (compare(i, t->left->key) < 0) {
        if (i < t->left->key) {
          y = t->left;                           /* rotate right */
          t->left = y->right;
          y->right = t;
          t->size = node_size(t->left) + node_size(t->right) + 1;
          t = y;
          if (t->left == NULL) break;
        }
        r->left = t;                               /* link right */
        r = t;
        t = t->left;
        r_size += 1+node_size(r->right);
      } else if (i > t->key) {
        if (t->right == NULL) break;
        
        //  if (compare(i, t->right->key) > 0) {
        if (i > t->right->key) {
          y = t->right;                          /* rotate left */
          t->right = y->left;
          y->left = t;
          t->size = node_size(t->left) + node_size(t->right) + 1;
          t = y;
          if (t->right == NULL) break;
        }
        l->right = t;                              /* link left */
        l = t;
        t = t->right;
        l_size += 1+node_size(l->left);
      } else {
        break;
      }
    }
    l_size += node_size(t->left);  /* Now l_size and r_size are the sizes of */
    r_size += node_size(t->right); /* the left and right trees we just built.*/
    t->size = l_size + r_size + 1;
    
    l->right = r->left = NULL;
    
    /* The following two loops correct the size fields of the right path  */
    /* from the left child of the root and the right path from the left   */
    /* child of the root.                                                 */
    for (y = N.right; y != NULL; y = y->right) {
      y->size = l_size;
      l_size -= 1+node_size(y->left);
    }
    for (y = N.left; y != NULL; y = y->left) {
      y->size = r_size;
      r_size -= 1+node_size(y->right);
    }
    
    l->right = t->left;                                /* assemble */
    r->left = t->right;
    t->left = N.right;
    t->right = N.left;
    
    return t;
  }
  
  // Insert key i into the tree t, if it is not already there.
  // Return a pointer to the resulting tree.
  //
  template <typename T>
  ComplexTree<T> * insert_node(T i, T j, ComplexTree<T> * t) {
    
    
    ComplexTree<T> * new_node;
    
    if (t != NULL) {
      
      t = splay(i,t);
      //if (compare(i, t->key)==0) {
      /* if (i == t->key){
       return t;  // it's already there
       }*/
    }
    //new_node = (ComplexTree *) malloc (sizeof (ComplexTree));
    //    if (new_node == NULL) {printf("Ran out of space\n"); exit(1);}
    new_node = new ComplexTree<T>();
    if(new_node == NULL)
      std::cout << "Object could not be allocated!\n";
    
    
    if (t == NULL) {
      
      new_node->left = new_node->right = NULL;
      
      //    } else if (compare(i, t->key) < 0) {
    } else if (i < t->key) {
      new_node->left = t->left;
      new_node->right = t;
      t->left = NULL;
      t->size = 1+node_size(t->right);
    } else {
      new_node->right = t->right;
      new_node->left = t;
      t->right = NULL;
      t->size = 1+node_size(t->left);
    }
    
    new_node->key = i;
    new_node->IssueCycle = j;
    new_node->size = 1 + node_size(new_node->left) + node_size(new_node->right);
    //New code
    //  new_node->occupacy = occupacy;
    return new_node;
  }
  
  
  
  
  // Deletes i from the ComplexTree if it's there.
  // Return a pointer to the resulting ComplexTree.
  //
  template <typename T>
  ComplexTree<T> * delete_node(T i, ComplexTree<T> *t) {

    ComplexTree<T> * x;
    size_t tsize;
    
    if (t==NULL) return NULL;
    tsize = t->size;
    


    t = splay(i,t);
  
    
    //if (compare(i, t->key) == 0) {               /* found it */
    if (i == t->key) {               /* found it */

      if (t->left == NULL) {

        x = t->right;
      } else {

        x = splay(i, t->left);
        x->right = t->right;
      }
      //free(t);
      delete t;
      if (x != NULL) {
        x->size = tsize-1;
        
      }
      return x;
    } else {
      return t;                         /* It wasn't there */
    }
  }
  
  
  

  
  template <typename T>
  bool find_key(T i, ComplexTree<T> *t ) {
    
    if (t != NULL) {
      t = splay(i,t);
      //if (compare(i, t->key) == 0) {               // found it
      if (i == t->key) {// found it
        
        return true;
      }else
        return false;
    }
    else{
      return false;
      
    }
  }

  
  template <typename T>
  ComplexTree<T> * delete_all (T i, ComplexTree<T> * t )
  {
    if ( t != NULL ) {
      t = delete_all ( t->left );
      t = delete_all ( t->right );
      if (t->key==i) {
        delete t;
        t = NULL;
        return t;
      }else
        return t;
      
      
    }
    
  }
  
  template <typename T>
  void delete_all(ComplexTree<T> *t)
  {
    if (t != NULL) {
      if (t->size == 1) {
        delete t;
        return;
      }
      if (t->left != NULL && t->left->size < t->size) {
        delete_all(t->left);
      }
      if (t->right != NULL && t->right->size < t->size) {
        delete_all(t->right);
      }
    }
  }

template <typename T>
  ComplexTree<T> * remove(ComplexTree<T> * node, T data)
   {
//std::cout << "Removing elements with data " << data << "\n";
     if(node == NULL)
     {
       return node;
     }

     if(data == node->IssueCycle)
     {
std::cout << "data == node->IssueCycle\n";
       ComplexTree<T> *retval = NULL;
    
       if(node->left == NULL)
       {
         retval = node->right;
         delete node;
         return retval;
       }
       else if(node->right == NULL)
       {
         retval = node->left;
         delete node;
         return retval;
       }
       else
       {
          ComplexTree<T> *successor = getSuccessor(node->left);
          node->key = successor->key;
        node->IssueCycle = successor->IssueCycle;
          node->left = remove(node->left, successor->IssueCycle);
       }
     }
     else if(data < node->IssueCycle)
     {
//std::cout << "data<node->IssueCycle\n";
       node->left = remove(node->left, data);
     }
     else
     {
//std::cout << "data< node->IssueCycle\n";
       node->right = remove(node->right, data);
     }

     return node;
   }

   template <typename T>
  ComplexTree<T> * getSuccessor( ComplexTree<T> *node)
   {
     while(node->right != NULL)
       node = node->right;
     return node;
   }


// Resmoves all nodes having value outside the given range and returns the root
// of modified tree
   template <typename T>
ComplexTree<T>* removeOutsideRange(ComplexTree<T> *root, T min, T max)
{
   // Base Case
   if (root == NULL)
      return NULL;
 
   // First fix the left and right subtrees of root
   root->left =  removeOutsideRange(root->left, min, max);
   root->right =  removeOutsideRange(root->right, min, max);
 
   // Now fix the root.  There are 2 possible cases for toot
   // 1.a) Root's key is smaller than min value (root is not in range)
std::cout << "root->IssueCycle " << root->IssueCycle << "\n";
   if (root->IssueCycle < min)
   {
std::cout << "root->IssueCycle " << root->IssueCycle << "\n";
       ComplexTree<T> *rChild = root->right;
       delete root;
       return rChild;
   }
   // 1.b) Root's key is greater than max value (root is not in range)
   if (root->IssueCycle > max)
   {
       ComplexTree<T> *lChild = root->left;
       delete root;
       return lChild;
   }
   // 2. Root is in range
   return root;
}

  
}
