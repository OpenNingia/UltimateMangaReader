#include "mangadex.h"

using namespace rapidjson;

MangaDex::MangaDex(NetworkManager *dm) : AbstractMangaSource(dm)
{
    name = "MangaDex";
    baseUrl = "https://mangadex.org";
    basedictUrl = baseUrl + "/titles/9/";

    networkManager->addCookie(".mangadex.org", "mangadex_h_toggle", "1");
    networkManager->addCookie(".mangadex.org", "mangadex_title_mode", "2");
    networkManager->addCookie(".mangadex.org", "mangadex_filter_langs", "1");

    statuses = {"Ongoing", "Completed", "Cancelled", "Hiatus"};
    demographies = {"Shounen", "Shoujo", "Seinen", "Josei"};
    genreMap.insert(2, "Action");
    genreMap.insert(3, "Adventure");
    genreMap.insert(5, "Comedy");
    genreMap.insert(8, "Drama");
    genreMap.insert(9, "Ecchi");
    genreMap.insert(10, "Fantasy");
    genreMap.insert(13, "Historical");
    genreMap.insert(14, "Horror");
    genreMap.insert(17, "Mecha");
    genreMap.insert(18, "Medical");
    genreMap.insert(20, "Mystery");
    genreMap.insert(22, "Psychological");
    genreMap.insert(23, "Romance");
    genreMap.insert(25, "Sci-Fi");
    genreMap.insert(28, "Shoujo Ai");
    genreMap.insert(30, "Shounen Ai");
    genreMap.insert(31, "Slice of Life");
    genreMap.insert(32, "Smut");
    genreMap.insert(33, "Sports");
    genreMap.insert(35, "Tragedy");
    genreMap.insert(37, "Yaoi");
    genreMap.insert(38, "Yuri");
    genreMap.insert(41, "Isekai");
    genreMap.insert(49, "Gore");
    genreMap.insert(50, "Sexual Violence");
    genreMap.insert(51, "Crime");
    genreMap.insert(52, "Magical Girls");
    genreMap.insert(53, "Philosophical");
    genreMap.insert(54, "Superhero");
    genreMap.insert(55, "Thriller");
    genreMap.insert(56, "Wuxia");
}

bool MangaDex::updateMangaList(UpdateProgressToken *token)
{
    QRegularExpression nummangasrx(R"(<p class=[^>]*>Showing .*? (\d+,\d+) titles)");

    QRegularExpression mangaidrx(R"lit(<a title="([^"]*?)"[^<]*?href=['"]/title/([^/]+)/[^<]*?class=")lit");

    MangaList mangas;

    auto job = networkManager->downloadAsString(basedictUrl + "1", -1);

    if (!job->await(7000))
    {
        token->sendError(job->errorString);
        return false;
    }

    token->sendProgress(10);

    QElapsedTimer timer;
    timer.start();

    auto nummangasrxmatch = nummangasrx.match(job->bufferStr);

    int nominalSize = 1;
    if (nummangasrxmatch.hasMatch())
        nominalSize = nummangasrxmatch.captured(1).remove(',').toInt();

    int pages = (nominalSize + 99) / 100;
    qDebug() << "pages" << pages;

    auto lambda = [&](QSharedPointer<DownloadStringJob> job) {
        int matches = 0;
        for (auto &match : getAllRxMatches(mangaidrx, job->bufferStr))
        {
            auto title = htmlToPlainText(match.captured(1));
            auto url = "/api/?type=manga&id=" + match.captured(2);
            mangas.append(title, url);
            matches++;
        }

        token->sendProgress(10 + 90 * mangas.size / nominalSize);

        qDebug() << "matches:" << matches;
    };

    lambda(job);

    QList<QString> urls;
    for (int i = 2; i <= pages; i++)
        urls.append(basedictUrl + QString::number(i));

    DownloadQueue queue(networkManager, urls, 2, lambda, true);
    queue.setCancellationToken(&token->canceled);
    queue.start();
    if (!queue.awaitCompletion())
    {
        token->sendError(queue.lastErrorMessage);
        return false;
    }
    this->mangaList = mangas;

    qDebug() << "mangas:" << mangas.size << "time:" << timer.elapsed();

    token->sendProgress(100);

    return true;
}

QString padChapterNumber(const QString &number, int places = 4)
{
    auto range = number.split('-');
    QStringList result;
    std::transform(range.begin(), range.end(), std::back_inserter(result), [places](QString chapter) {
        chapter = chapter.trimmed();
        auto digits = chapter.split('.')[0].length();
        return QString("0").repeated(qMax(0, places - digits)) + chapter;
    });
    return result.join('-');
}

Result<MangaChapterCollection, QString> MangaDex::updateMangaInfoFinishedLoading(
    QSharedPointer<DownloadStringJob> job, QSharedPointer<MangaInfo> info)
{
    //    QElapsedTimer t;
    //    t.start();
    QRegularExpression bbrx(R"(\[.*?\])");

    MangaChapterCollection newchapters;

    try
    {
        Document doc;
        ParseResult res = doc.Parse(job->buffer.data());
        if (!res)
            return Err(QString("Coulnd't parse mangainfos."));

        auto &mangaObject = doc["manga"];

        info->author = htmlToPlainText(QString(mangaObject["author"].GetString()));
        info->artist = htmlToPlainText(QString(mangaObject["artist"].GetString()));

        int statusi = mangaObject["status"].GetInt64();

        if (statusi >= 0 && statusi < statuses.length())
            info->status = statuses[statusi];

        info->releaseYear = "";

        info->genres = "";
        auto genresArray = mangaObject["genres"].GetArray();
        for (const auto &g : genresArray)
        {
            int gn = g.GetInt64();
            if (genreMap.contains(gn))
                info->genres += genreMap[gn] + " ";
        }

        info->summary = htmlToPlainText(QString(mangaObject["description"].GetString())).remove(bbrx);

        info->coverUrl = baseUrl + QString(mangaObject["cover_url"].GetString());

        auto &chaptersObject = doc["chapter"];

        for (auto it = chaptersObject.MemberBegin(); it != chaptersObject.MemberEnd(); ++it)
        {
            auto chapterKey = it->name.GetString();
            auto &chapterObject = it->value;
            auto language = QString(chapterObject["lang_code"].GetString());

            if (language != "gb")
                continue;

            auto numChapter = QString(chapterObject["chapter"].GetString());

            auto chapterTitle = "Ch. " + numChapter + " " + QString(chapterObject["title"].GetString());

            auto chapterUrl = QString("https://mangadex.org/api/?type=chapter&id=") + chapterKey;

            MangaChapter mangaChapter(chapterTitle, chapterUrl);
            mangaChapter.chapterNumber = padChapterNumber(numChapter);

            newchapters.insert(0, mangaChapter);
        }
    }
    catch (QException &)
    {
        return Err(QString("Coulnd't parse mangainfos."));
    }
    //    qDebug() << "Mangadex update:" << t.elapsed() << info->title;

    std::sort(newchapters.begin(), newchapters.end(),
              [](auto a, auto b) { return a.chapterNumber < b.chapterNumber; });

    return Ok(newchapters);
}

Result<QStringList, QString> MangaDex::getPageList(const QString &chapterUrl)
{
    auto job = networkManager->downloadAsString(chapterUrl);

    if (!job->await(7000))
        return Err(job->errorString);

    QStringList imageUrls;

    try
    {
        Document doc;
        ParseResult res = doc.Parse(job->buffer.data());
        if (!res)
            return Err(QString("Coulnd't parse pagelist."));

        auto hash = QString(doc["hash"].GetString());

        auto server = QString(doc["server"].GetString());

        auto pagesArray = doc["page_array"].GetArray();

        for (const auto &page : pagesArray)
            imageUrls.append(server + hash + "/" + page.GetString());
    }
    catch (QException &)
    {
        return Err(QString("Coulnd't parse pagelist."));
    }

    return Ok(imageUrls);
}
