#include "genexport.h"
#include "../reach/paint.h"
#include "../type/assemble.h"

#ifdef PLATFORM_IS_POSIX_BASED
#  include <unistd.h>
#endif

static bool exported_methods(compile_t* c, ast_t* ast)
{
  ast_t* type = ast_child(ast);

  ast_t* def = (ast_t*)ast_data(type);
  ast_t* members = ast_childidx(def, 4);
  ast_t* member = ast_child(members);

  while(member != NULL)
  {
    switch(ast_id(member))
    {
      case TK_NEW:
      case TK_BE:
      case TK_FUN:
      {
        AST_GET_CHILDREN(member, cap, m_id, typeparams);

        // Mark all non-polymorphic methods as reachable.
        if(ast_id(typeparams) == TK_NONE)
          reach_export(c->reach, type, ast_name(m_id), NULL, c->opt, NULL);

        break;
      }

      default: {}
    }

    member = ast_sibling(member);
  }

  ast_free_unattached(type);
  return true;
}

static bool exported_classes(compile_t* c, ast_t* program)
{
  if(c->opt->verbosity >= VERBOSITY_INFO)
    fprintf(stderr, " Export reachability\n");

  ast_t* package = ast_child(program);

  while(package != NULL)
  {
    ast_t* module = ast_child(package);

    while(module != NULL)
    {
      ast_t* entity = ast_child(module);

      while(entity != NULL)
      {
        if(ast_id(entity) == TK_EXPORT)
        {
          exported_methods(c, entity);
        }

        entity = ast_sibling(entity);
      }

      module = ast_sibling(module);
    }

    package = ast_sibling(package);
  }

  if(c->opt->verbosity >= VERBOSITY_INFO)
    fprintf(stderr, " Export selector painting\n");
  paint(&c->reach->types);

  return true;
}

bool genexport(compile_t* c, ast_t* program)
{
  return exported_classes(c, program);
}
