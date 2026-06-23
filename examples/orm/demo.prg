/* demo.prg -- console demo: ORM end-to-end over the OpenADS ACE ABI.
   Connects via sqlite:// so every call lands on ace64.dll.
   Build with build_run.bat (see that file for env requirements).
   API used: TORMConnection / TORMSchema / TORMModel / TORMQuery /
             ORM_HasMany / TORMMigration / TORMMigrator */
#include "hborm.ch"

/* ---- Model definitions ----------------------------------------- */

CREATE CLASS TAutor FROM TORMModel
   METHOD TableName() INLINE "autores"
   METHOD Relations() INLINE { ;
      "livros" => ORM_HasMany( {|c| TLivro():New( c )}, "autor_id" ) }
END CLASS

/* In the SQL passthrough path numeric/decimal columns come back as
   strings from the engine.  Declaring Casts() tells the ORM to
   coerce them to the correct Harbour type on read. */
CREATE CLASS TLivro FROM TORMModel
   METHOD TableName() INLINE "livros"
   METHOD Casts()     INLINE { "preco" => "decimal:2" }
END CLASS

/* ================================================================= */

PROCEDURE Main()
   LOCAL oConn, oSchema
   LOCAL oA1, oA2
   LOCAL aRows, xTotal, nQt, hR
   LOCAL aAutores, oAut, aLivros, oLivro
   LOCAL lOk

   /* ---- 1. HEADER ------------------------------------------------ */
   ? ""
   ? "========================================================"
   ? "  hb_orm2 -- ORM demo over OpenADS ACE ABI (ace64.dll)"
   ? "========================================================"
   ?

   /* ---- 2. CONNECT via OpenADS engine ---------------------------- */
   ? "[ connect ]"
   oConn := TORMConnection():New( "sqlite://orm_ace_demo.db" )
   IF ! oConn:IsOpen()
      ? "  ERROR: could not open connection"
      RETURN
   ENDIF
   ? "  class   :", oConn:ClassName()
   ? "  nav?    :", iif( oConn:IsNavigational(), "yes (DBF/cursor)", "no (SQL passthrough)" )
   ? "  note    : every call lands on ace64.dll -- this is NOT the direct driver"
   ?

   /* ---- idempotency: drop demo tables if they exist -------------- */
   oConn:Execute( "DROP TABLE IF EXISTS livros",  {} )
   oConn:Execute( "DROP TABLE IF EXISTS autores", {} )

   /* ---- 3. SCHEMA ------------------------------------------------ */
   ? "[ schema -- CreateTable ]"
   oSchema := TORMSchema():New( oConn )

   lOk := oSchema:CreateTable( "autores", {| t | ;
      t:Id(), ;
      t:String( "nome", 60 ):Nullable( .F. ), ;
      t:String( "pais",  2 ) } )
   ? "  autores created:", iif( lOk, "OK", "FAILED" )

   lOk := oSchema:CreateTable( "livros", {| t | ;
      t:Id(), ;
      t:Integer( "autor_id" ), ;
      t:String( "titulo", 80 ):Nullable( .F. ), ;
      t:Decimal( "preco", 8, 2 ) } )
   ? "  livros  created:", iif( lOk, "OK", "FAILED" )
   ?

   /* ---- 4. INSERT (ActiveRecord) --------------------------------- */
   ? "[ insert -- ActiveRecord Create() ]"
   oA1 := TAutor():New( oConn )
   oA1:Create( { "id" => 1, "nome" => "Maria Silva", "pais" => "BR" } )
   ? "  autor 1 id:", oA1:Get( "id" )

   oA2 := TAutor():New( oConn )
   oA2:Create( { "id" => 2, "nome" => "John Doe", "pais" => "US" } )
   ? "  autor 2 id:", oA2:Get( "id" )

   TLivro():New( oConn ):Create( { "id" => 10, "autor_id" => 1, "titulo" => "Habitos Atomicos",        "preco" => 49.90 } )
   TLivro():New( oConn ):Create( { "id" => 11, "autor_id" => 1, "titulo" => "Foco Total",               "preco" => 39.90 } )
   TLivro():New( oConn ):Create( { "id" => 12, "autor_id" => 2, "titulo" => "Clean Code",               "preco" => 89.00 } )
   TLivro():New( oConn ):Create( { "id" => 13, "autor_id" => 2, "titulo" => "The Pragmatic Programmer", "preco" => 79.00 } )
   TLivro():New( oConn ):Create( { "id" => 14, "autor_id" => 2, "titulo" => "Refactoring",              "preco" => 29.90 } )
   ? "  5 livros inserted"
   ?

   /* ---- 5. FIND + UPDATE ---------------------------------------- */
   ? "[ find + update -- Find() / Set() / Save() ]"
   oLivro := TLivro():New( oConn ):Find( 14 )
   IF oLivro != NIL
      ? "  found id=14 preco:", oLivro:Get( "preco" ), "(decimal cast applied)"
      oLivro:Set( "preco", 34.90 )
      oLivro:Save()
      ? "  updated preco:", TLivro():New( oConn ):Find( 14 ):Get( "preco" )
   ENDIF
   ?

   /* ---- 6. QUERY BUILDER ---------------------------------------- */
   ? "[ query builder -- Where / OrderBy / Get / Sum / Count ]"

   /* filter + order */
   aRows := TORMQuery():New( oConn, "livros" ) ;
               :Where( "preco", ">", 35 ) ;
               :OrderBy( "preco", "DESC" ) ;
               :Get()
   ? "  livros com preco > 35 (mais caro primeiro):"
   FOR EACH hR IN aRows
      ? "    id=" + hb_CStr( hR[ "id" ] ) + ;
        "  preco=" + hb_CStr( hR[ "preco" ] ) + ;
        "  titulo=" + AllTrim( hb_CStr( hR[ "titulo" ] ) )
   NEXT

   /* aggregates */
   xTotal := TORMQuery():New( oConn, "livros" ):Sum( "preco" )
   ? "  total preco (Sum):", xTotal

   nQt := TORMQuery():New( oConn, "livros" ):Count()
   ? "  total livros (Count):", nQt
   ?

   /* ---- 7. RELATIONS + EAGER LOAD ------------------------------- */
   ? "[ relations -- eager hasMany (anti-N+1) ]"

   /* 1 query base + 1 WhereIn livros = 2 queries total, NOT 1+N */
   oConn:ResetQueryCount()
   aAutores := TAutor():New( oConn ):Com( "livros" ):Todos()
   ? "  queries fired:", oConn:QueryCount(), "(expected 2 -- anti-N+1 proven)"

   FOR EACH oAut IN aAutores
      aLivros := oAut:Rel( "livros" )
      ? "  " + AllTrim( hb_CStr( oAut:Get( "nome" ) ) ) + ;
        " (" + AllTrim( hb_CStr( oAut:Get( "pais" ) ) ) + ")" + ;
        " -> " + hb_CStr( Len( aLivros ) ) + " livro(s)"
   NEXT
   ?

   /* ---- 8. BILINGUAL API (PT / EN) ------------------------------ */
   ? "[ bilingual API note ]"
   ? "  All verbs exist in both PT and EN:"
   ? "  Find/Buscar  Save/Salvar  Create/Criar  Delete/Deletar"
   ? "  All/Todos    With/Com     Where/Onde    OrderBy/OrdenarPor"
   ?

   /* ---- 9. FOOTER ----------------------------------------------- */
   oConn:Close()
   ? "========================================================"
   ? "  demo OK"
   ? "========================================================"
   ?
   RETURN
