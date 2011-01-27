/*
 * Copyright (c) 2011, BoostPro Computing.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the
 *   distribution.
 *
 * - Neither the name of BoostPro Computing nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gitutil.h"

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/fstream.hpp>

#ifndef ASSERTS
#undef assert
#define assert(x)
#endif

namespace Git
{
  void Tree::do_update(boost::filesystem::path::iterator segment,
                       boost::filesystem::path::iterator end, ObjectPtr obj)
  {
    std::string name = (*segment).string();

    modified = true;

    entries_map::iterator i = entries.find(name);
    if (++segment == end) {
      assert(name == obj->name);

      if (i == entries.end()) {
        written = false;        // force the whole tree to be rewritten
        entries.insert(entries_pair(name, obj));
      } else {
        // If the object we're updating is just a blob, the tree doesn't
        // need to be regnerated entirely, it will just get updated by
        // git_object_write.
        if (obj->is_blob()) {
          ObjectPtr curr_obj = (*i).second;
          git_tree_entry_set_id(curr_obj->tree_entry, *obj);
          git_tree_entry_set_attributes(curr_obj->tree_entry, obj->attributes);
          if (name != obj->name) {
            sort_needed = true;
            entries.erase(i);
            entries.insert(entries_pair(obj->name, obj));
            git_tree_entry_set_name(curr_obj->tree_entry, obj->name.c_str());
          } else {
            (*i).second = obj;
          }
          obj->tree_entry = curr_obj->tree_entry;
        } else {
          written = false;      // force the whole tree to be rewritten
          (*i).second = obj;
        }
      }
    } else {
      TreePtr tree;
      if (i == entries.end()) {
        tree = repository->create_tree(name);
        entries.insert(entries_pair(name, tree));
      } else {
        tree = dynamic_cast<Tree *>((*i).second.get());
      }

      tree->do_update(segment, end, obj);

      written = false;          // force the whole tree to be rewritten
    }
  }

  void Tree::do_remove(boost::filesystem::path::iterator segment,
                       boost::filesystem::path::iterator end)
  {
    std::string name = (*segment).string();

    modified = true;

    entries_map::iterator i = entries.find(name);
    // It's OK for remove not to find what it's looking for, because it
    // may be that Subversion wishes to remove an empty directory, which
    // would never have been added in the first place.
    if (i != entries.end()) {
      if (++segment == end) {
        entries.erase(i);
        if (git_tree_remove_entry_byname(*this, name.c_str()) != 0)
          throw std::logic_error("Could not remove entry from tree");
      } else {
        TreePtr subtree = dynamic_cast<Tree *>((*i).second.get());
        subtree->do_remove(segment, end);
        if (subtree->empty()) {
          entries.erase(i);
          if (git_tree_remove_entry_byname(*this, name.c_str()) != 0)
            throw std::logic_error("Could not remove entry from tree");
        } else {
          written = false;      // force the whole tree to be rewritten
        }
      }
    }
  }

  void Tree::write()
  {
    if (empty()) return;

    if (written) {
      if (modified) {
        if (sort_needed)
          git_tree_sort_entries(*this);
        if (git_object_write(*this) != 0)
          throw std::logic_error("Could not write tree object");
      }
    } else {
      // written may be false now because of a change which requires us
      // to rewrite the entire tree.  To be on the safe side, just clear
      // out any existing entries in the git tree, and rewrite the
      // entries known to the Tree object.
      clear();

      for (entries_map::const_iterator i = entries.begin();
           i != entries.end();
           ++i) {
        ObjectPtr obj((*i).second);
        if (! obj->is_blob())
          obj->write();
        if (git_tree_add_entry2(&obj->tree_entry, *this, *obj,
                                obj->name.c_str(), obj->attributes) != 0)
          throw std::logic_error("Could not add entry to tree");
      }
      git_tree_sort_entries(*this);
      if (git_object_write(*this) != 0)
        throw std::logic_error("Could not write tree object");
    }
    written  = true;
    modified = false;
  }

  void Commit::update(const boost::filesystem::path& pathname, ObjectPtr obj)
  {
    if (! tree)
      tree = repository->create_tree((*pathname.begin()).string());
    tree->update(pathname, obj);
  }

  CommitPtr Commit::clone()
  {
    CommitPtr new_commit = repository->create_commit();

    new_commit->tree   = tree;
    new_commit->prefix = prefix;
    new_commit->add_parent(this);

    return new_commit;
  }

  void Commit::write()
  {
    assert(tree);

    TreePtr subtree;
    if (prefix.empty()) {
      subtree = tree;
    } else {
      ObjectPtr obj = tree->lookup(prefix);
      assert(obj->is_tree());
      subtree = dynamic_cast<Tree *>(obj.get());
    }
    if (! subtree)
      subtree = tree = repository->create_tree();

    assert(! subtree->empty());
    subtree->write();

    git_commit_set_tree(*this, *subtree);

    if (git_object_write(*this) != 0)
      throw std::logic_error("Could not write out Git commit");
  }

  void Branch::update(Repository& repository, CommitPtr _commit)
  {
    commit = _commit;
    commit->write();
    repository.create_file(boost::filesystem::path("refs") / "heads" / name,
                           commit->sha1());
  }

  CommitPtr Repository::read_commit(const git_oid * oid)
  {
    git_commit * git_commit;
    
    if (git_commit_lookup(&git_commit, *this, oid) != 0)
      throw std::logic_error("Could not find Git commit");

    CommitPtr commit = new Commit(this, git_commit);

    // jww (2011-01-27): NYI: Load tree data from the Git repository for
    // this commit.
    return commit;
  }

  void Repository::create_file(const boost::filesystem::path& pathname,
                               const std::string& content)
  {
    boost::filesystem::path dir(boost::filesystem::current_path());
    boost::filesystem::path file(boost::filesystem::path(".git") / pathname);
    boost::filesystem::path parent(file.parent_path());

    // Make sure the directory exists for the file

    for (boost::filesystem::path::iterator i = parent.begin();
         i != parent.end();
         ++i) {
      dir /= *i;
      if (! boost::filesystem::is_directory(dir)) {
        boost::filesystem::create_directory(dir);
        if (! boost::filesystem::is_directory(dir))
          throw std::logic_error(std::string("Directory ") + dir.string() +
                                 " does not exist and could not be created");
      }
    }

    // Make sure where we want to write isn't a directory or something

    file = boost::filesystem::current_path() / file;

    if (boost::filesystem::exists(file) &&
        ! boost::filesystem::is_regular_file(file))
      throw std::logic_error(file.string() +
                             " already exists but is not a regular file");

    // Open the file, creating it if it doesn't already exist

    boost::filesystem::ofstream out(file);
    out << content;
    out.close();
  }
}